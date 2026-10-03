#include <cjson/cJSON.h>

#include "scene_2d.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/passes/pass_loader.h"
#include "shared/graphics/passes/shader_pass.h"
#include "shared/graphics/render.h"
#include "sokol_app.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

void Scene2DRuntime::drawDirect() {
    const bool has_output_viewport = output_width > 0 && output_height > 0;
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, output_width, output_height, true);
        sg_apply_scissor_rect(output_x, output_y, output_width, output_height, true);
    }

    if (ctx.debug.test_mode && ctx.debug.selected_object >= 0 &&
        ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        ctx.scene.layers[ctx.debug.selected_object]->draw(ctx);
    } else {
        bool any_solo = false;
        for (auto layer : ctx.scene.layers) {
            if (layer->solo) {
                any_solo = true;
                break;
            }
        }

        for (auto layer : ctx.scene.layers) {
            if (any_solo) {
                if (layer->solo) layer->draw(ctx);
            } else if (layer->visible) {
                layer->draw(ctx);
            }
        }
    }

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, sapp_width(), sapp_height(), true);
        sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
    }
}

void Scene2DRuntime::drawOffscreen() {
    const int width = output_width > 0 ? output_width : sapp_width();
    const int height = output_height > 0 ? output_height : sapp_height();
    if (!ensureSceneTargets(width, height)) {
        scene_output_index = -1;
        return;
    }

    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);

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

        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        sg_image snapshot = sg_make_image(&image_desc);
        if (snapshot.id == SG_INVALID_ID) return;
        sg_view_desc source_view_desc = {};
        source_view_desc.texture.image = snapshot;
        sg_view snapshot_texture = sg_make_view(&source_view_desc);
        sg_view_desc attachment_view_desc = {};
        attachment_view_desc.color_attachment.image = snapshot;
        sg_view snapshot_attachment = sg_make_view(&attachment_view_desc);
        if (snapshot_texture.id == SG_INVALID_ID || snapshot_attachment.id == SG_INVALID_ID) {
            if (snapshot_texture.id != SG_INVALID_ID) sg_destroy_view(snapshot_texture);
            if (snapshot_attachment.id != SG_INVALID_ID) sg_destroy_view(snapshot_attachment);
            sg_destroy_image(snapshot);
            return;
        }

        sg_pass snapshot_pass = {};
        snapshot_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        snapshot_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        snapshot_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
        snapshot_pass.attachments.colors[0] = snapshot_attachment;
        sg_begin_pass(&snapshot_pass);
        if (raw_layer) {
            // Capture the layer's own output before it blends into the accumulated scene.
            layer->draw(ctx);
        } else {
            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0.0f, 0.0f, (float)width, (float)height, 0.0f, white, false, nullptr);
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
        diagnostics.recordSceneStage(stage_name, snapshot, snapshot_texture, snapshot_attachment);
        if (!raw_layer) ++layer_index;
    };

    auto draw_layer = [&](Layer* layer) {
        auto* particle = dynamic_cast<ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) {
            particle->setSceneColorView(scene_targets[current].texture_view);
            capture_layer_result(layer, true);
            const int next = 1 - current;

            sg_pass composite_pass = {};
            composite_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            composite_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            composite_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
            composite_pass.attachments.colors[0] = scene_targets[next].attachment_view;
            sg_begin_pass(&composite_pass);

            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0.0f, 0.0f, (float)width, (float)height, 0.0f, white, false, nullptr);
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

            sg_pass composite_pass = {};
            composite_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            composite_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            composite_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
            composite_pass.attachments.colors[0] = scene_targets[next].attachment_view;
            sg_begin_pass(&composite_pass);
            float white[4] = {1, 1, 1, 1};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0, 0, (float)width, (float)height, 0, white, false, nullptr);
            image->drawComposite(ctx, scene_targets[current].texture_view);
            sg_end_pass();

            current = next;
            capture_layer_result(layer, false);
            return;
        }

        capture_layer_result(layer, true);

        sg_pass layer_pass = {};
        layer_pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        layer_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        layer_pass.attachments.colors[0] = scene_targets[current].attachment_view;
        sg_begin_pass(&layer_pass);
        layer->draw(ctx);
        sg_end_pass();

        capture_layer_result(layer, false);
    };

    if (ctx.debug.test_mode && ctx.debug.selected_object >= 0 &&
        ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        draw_layer(ctx.scene.layers[ctx.debug.selected_object]);
    } else {
        bool any_solo = false;
        for (auto layer : ctx.scene.layers) {
            if (layer->solo) {
                any_solo = true;
                break;
            }
        }
        for (auto layer : ctx.scene.layers) {
            if ((any_solo && !layer->solo) || (!any_solo && !layer->visible)) continue;
            draw_layer(layer);
        }
    }

    current = renderBloom(current, width, height);
    // The layer snapshots stop before post-processing; capture one final post-bloom
    // stage so diagnostics represent what present() sends to the swapchain.
    {
        IRenderObserver& diagnostics = renderObserver();
        if (diagnostics.isCapturingFrame()) {
            sg_image_desc image_desc = {};
            image_desc.usage.color_attachment = true;
            image_desc.width = width;
            image_desc.height = height;
            image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            sg_image snapshot = sg_make_image(&image_desc);
            if (snapshot.id != SG_INVALID_ID) {
                sg_view_desc texture_desc = {};
                texture_desc.texture.image = snapshot;
                sg_view texture_view = sg_make_view(&texture_desc);
                sg_view_desc attachment_desc = {};
                attachment_desc.color_attachment.image = snapshot;
                sg_view attachment_view = sg_make_view(&attachment_desc);
                if (texture_view.id != SG_INVALID_ID && attachment_view.id != SG_INVALID_ID) {
                    sg_pass snapshot_pass = {};
                    snapshot_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
                    snapshot_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
                    snapshot_pass.attachments.colors[0] = attachment_view;
                    sg_begin_pass(&snapshot_pass);
                    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image,
                                         scene_targets[current].texture_view, 0.0f, 0.0f, (float)width, (float)height,
                                         0.0f, white, false, nullptr);
                    sg_end_pass();
                    diagnostics.recordSceneStage("post-bloom-final", snapshot, texture_view, attachment_view);
                } else {
                    if (texture_view.id != SG_INVALID_ID) sg_destroy_view(texture_view);
                    if (attachment_view.id != SG_INVALID_ID) sg_destroy_view(attachment_view);
                    sg_destroy_image(snapshot);
                }
            }
        }
    }
    scene_output_index = current;
}
