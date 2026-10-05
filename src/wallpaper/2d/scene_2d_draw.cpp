#include <cjson/cJSON.h>

#include <algorithm>

#include "scene_2d.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/surface.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/pass_util.h"
#include "shared/graphics/passes/pass_loader.h"
#include "shared/graphics/passes/shader_pass.h"
#include "shared/graphics/render.h"
#include "sokol_app.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"
#include "wallpaper/2d/tree/scene_visibility.h"

namespace {
void drawFullscreenTarget(EngineContext& ctx, sg_image image, sg_view texture_view, int, int) {
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, image, texture_view, 0.0f, 0.0f, ctx.renderer.view_width,
                         ctx.renderer.view_height, 0.0f, white, false, nullptr);
}

struct Snapshot {
    sg_image image = {SG_INVALID_ID};
    sg_view texture = {SG_INVALID_ID};
    sg_view attachment = {SG_INVALID_ID};

    bool valid() const {
        return image.id != SG_INVALID_ID && texture.id != SG_INVALID_ID && attachment.id != SG_INVALID_ID;
    }
    void destroy() {
        if (texture.id != SG_INVALID_ID) sg_destroy_view(texture);
        if (attachment.id != SG_INVALID_ID) sg_destroy_view(attachment);
        if (image.id != SG_INVALID_ID) sg_destroy_image(image);
        *this = Snapshot();
    }
};

Snapshot makeSnapshot(int width, int height) {
    Snapshot snapshot;
    sg_image_desc image_desc = {};
    image_desc.usage.color_attachment = true;
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    snapshot.image = sg_make_image(&image_desc);
    if (snapshot.image.id == SG_INVALID_ID) return snapshot;

    sg_view_desc texture_desc = {};
    texture_desc.texture.image = snapshot.image;
    snapshot.texture = sg_make_view(&texture_desc);
    sg_view_desc attachment_desc = {};
    attachment_desc.color_attachment.image = snapshot.image;
    snapshot.attachment = sg_make_view(&attachment_desc);
    if (!snapshot.valid()) snapshot.destroy();
    return snapshot;
}

template <class Draw>
void forEachDrawnLayer(EngineContext& ctx, Draw&& draw) {
    if (ctx.debug.test_mode && ctx.debug.selected_object >= 0 &&
        ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        draw(ctx.scene.layers[ctx.debug.selected_object]);
        return;
    }
    const bool any_solo =
        std::any_of(ctx.scene.layers.begin(), ctx.scene.layers.end(), [](const Layer* layer) { return layer->solo; });
    const SceneVisibility visibility(ctx);
    for (Layer* layer : ctx.scene.layers) {
        if (any_solo ? layer->solo : visibility.visible(*layer)) draw(layer);
    }
}
}  // namespace

void Scene2DRuntime::drawDirect() {
    const bool has_output_viewport = output_width > 0 && output_height > 0;
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, output_width, output_height, true);
        sg_apply_scissor_rect(output_x, output_y, output_width, output_height, true);
    }

    forEachDrawnLayer(ctx, [&](Layer* layer) { layer->draw(ctx); });

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, surface::width(), surface::height(), true);
        sg_apply_scissor_rect(0, 0, surface::width(), surface::height(), true);
    }
}

void Scene2DRuntime::drawOffscreen() {
    const int width = output_width > 0 ? output_width : surface::width();
    const int height = output_height > 0 ? output_height : surface::height();
    if (!ensureSceneTargets(width, height)) {
        scene_output_index = -1;
        return;
    }

    updateViewport();

    int current = 0;
    sg_pass clear_pass = {};
    clear_pass.action = ctx.pass_action;
    clear_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    clear_pass.attachments.colors[0] = scene_targets[current].attachment_view;
    sg_begin_pass(&clear_pass);
    sg_end_pass();

    int layer_index = 0;
    auto capture_layer_result = [&](Layer* layer, bool raw_layer) {
        IRenderObserver& diagnostics = renderObserver();
        if (!diagnostics.isCapturingFrame()) return;

        Snapshot snapshot = makeSnapshot(width, height);
        if (!snapshot.valid()) return;

        sg_pass snapshot_pass = colorPass(snapshot.attachment, SG_LOADACTION_CLEAR);
        sg_begin_pass(&snapshot_pass);
        if (raw_layer) {
            // Capture the layer's own output before it blends into the accumulated scene.
            layer->draw(ctx);
        } else {
            drawFullscreenTarget(ctx, scene_targets[current].image, scene_targets[current].texture_view, width, height);
        }
        sg_end_pass();

        char stage_name[320];
        if (const auto* image = dynamic_cast<const ImageLayer*>(layer)) {
            snprintf(stage_name, sizeof(stage_name), "%s-%02d-id-%u-%s-alpha-%.3f-blend-%d",
                     raw_layer ? "raw" : "after", layer_index, image->scene_object_id, layer->name.c_str(),
                     image->tint[3], image->color_blend_mode);
        } else {
            snprintf(stage_name, sizeof(stage_name), "%s-%02d-id-%u-%s", raw_layer ? "raw" : "after", layer_index,
                     layer->scene_object_id, layer->name.c_str());
        }
        diagnostics.recordSceneStage(stage_name, snapshot.image, snapshot.texture, snapshot.attachment);
        if (!raw_layer) ++layer_index;
    };

    auto draw_layer = [&](Layer* layer) {
        auto* particle = dynamic_cast<ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) {
            particle->setSceneColorView(scene_targets[current].texture_view);
            capture_layer_result(layer, true);
            const int next = 1 - current;

            sg_pass composite_pass = colorPass(scene_targets[next].attachment_view, SG_LOADACTION_CLEAR);
            sg_begin_pass(&composite_pass);
            drawFullscreenTarget(ctx, scene_targets[current].image, scene_targets[current].texture_view, width, height);
            particle->setSceneColorView(scene_targets[current].texture_view);
            if (!RenderDiagnostics::instance().getConfig().disable_particles) {
                particle->draw(ctx);
            }
            sg_end_pass();

            current = next;
            capture_layer_result(layer, false);
            return;
        }

        auto* image = dynamic_cast<ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) {
            if (image->is_fullscreen && !image->effects.empty()) {
                image->renderEffectChain(ctx, scene_targets[current].image, scene_targets[current].texture_view);
            } else if (image->is_compose_region && !image->effects.empty()) {
                image->renderRegionEffectChain(ctx, scene_targets[current].image, scene_targets[current].texture_view);
            }
            capture_layer_result(layer, true);
            const int next = 1 - current;

            sg_pass composite_pass = colorPass(scene_targets[next].attachment_view, SG_LOADACTION_CLEAR, 1.0f);
            sg_begin_pass(&composite_pass);
            drawFullscreenTarget(ctx, scene_targets[current].image, scene_targets[current].texture_view, width, height);
            image->drawComposite(ctx, scene_targets[current].texture_view);
            sg_end_pass();

            current = next;
            capture_layer_result(layer, false);
            return;
        }

        capture_layer_result(layer, true);

        sg_pass layer_pass = colorPass(scene_targets[current].attachment_view, SG_LOADACTION_LOAD);
        sg_begin_pass(&layer_pass);
        layer->draw(ctx);
        sg_end_pass();

        capture_layer_result(layer, false);
    };

    forEachDrawnLayer(ctx, draw_layer);

    current = renderBloom(current, width, height);
    // The layer snapshots stop before post-processing; capture one final post-bloom
    // stage so diagnostics represent what present() sends to the swapchain.
    if (IRenderObserver& diagnostics = renderObserver(); diagnostics.isCapturingFrame()) {
        Snapshot snapshot = makeSnapshot(width, height);
        if (snapshot.valid()) {
            sg_pass snapshot_pass = colorPass(snapshot.attachment, SG_LOADACTION_CLEAR);
            sg_begin_pass(&snapshot_pass);
            drawFullscreenTarget(ctx, scene_targets[current].image, scene_targets[current].texture_view, width, height);
            sg_end_pass();
            diagnostics.recordSceneStage("post-bloom-final", snapshot.image, snapshot.texture, snapshot.attachment);
        }
    }
    scene_output_index = current;
}
