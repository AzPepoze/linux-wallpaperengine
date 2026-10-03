#include "scene_2d.h"

#include <cjson/cJSON.h>

#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/surface.h"
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

void Scene2DRuntime::init() {
    renderer_init(&ctx.renderer, (float)surface::width(), (float)surface::height());
    // DO NOT EDIT: must precompile all blend pipelines here to prevent GPU context loss mid-render (crash fix)
    renderer_precompile_blend_pipelines(ctx, &ctx.renderer);
}

void Scene2DRuntime::update(float dt) {
    if (ctx.debug.test_mode && ctx.debug.selected_object >= 0 &&
        ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        ctx.scene.layers[ctx.debug.selected_object]->update(dt, ctx);
        return;
    }
    for (auto layer : ctx.scene.layers) layer->update(dt, ctx);
}

bool Scene2DRuntime::requiresOffscreenComposition() const {
    // Direct drawing has no readable target, so captures render offscreen to expose per-layer stages.
    if (renderObserver().isCapturingFrame()) return true;

    if (!RenderDiagnostics::instance().getConfig().disable_bloom) {
        const float bloom_strength =
            ctx.scene.general.hdr ? ctx.scene.general.bloom.hdr_strength : ctx.scene.general.bloom.strength;
        if (ctx.scene.general.bloom.enabled && bloom_strength > 0.0f) return true;
    }

    if (ctx.debug.test_mode && ctx.debug.selected_object >= 0 &&
        ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        const auto* particle = dynamic_cast<const ParticleLayer*>(ctx.scene.layers[ctx.debug.selected_object]);
        return particle && particle->requiresSceneColor();
    }

    bool any_solo = false;
    for (const auto* layer : ctx.scene.layers) {
        if (layer->solo) {
            any_solo = true;
            break;
        }
    }

    for (const auto* layer : ctx.scene.layers) {
        if ((any_solo && !layer->solo) || (!any_solo && !layer->visible)) continue;
        const auto* particle = dynamic_cast<const ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) return true;
        const auto* image = dynamic_cast<const ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) return true;
    }
    return false;
}

sg_pixel_format Scene2DRuntime::compositionPixelFormat() const {
    if (!ctx.scene.general.hdr) return SG_PIXELFORMAT_RGBA8;
    // Float attachment preserves HDR bloom energy; drivers without it fail creation and retry RGBA8.
    return SG_PIXELFORMAT_RGBA16F;
}

bool Scene2DRuntime::ensureSceneTargets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const sg_pixel_format requested_format = compositionPixelFormat();
    if (scene_targets[0].image.id != SG_INVALID_ID && scene_targets[0].width == width &&
        scene_targets[0].height == height && scene_targets[1].image.id != SG_INVALID_ID &&
        scene_targets[1].width == width && scene_targets[1].height == height &&
        scene_targets[0].pixel_format == requested_format && scene_targets[1].pixel_format == requested_format) {
        return true;
    }

    scene_targets[0].reset();
    scene_targets[1].reset();

    if (!scene_targets[0].create(width, height, requested_format) ||
        !scene_targets[1].create(width, height, requested_format)) {
        scene_targets[0].reset();
        scene_targets[1].reset();
        return false;
    }
    return true;
}

void Scene2DRuntime::draw() {
    if (requiresOffscreenComposition())
        drawOffscreen();
    else {
        scene_output_index = -1;
        drawDirect();
    }
}

void Scene2DRuntime::drawParticleDiagnostics() {
    if (!ctx.debug.particle_debug_bounds && !ctx.debug.particle_debug_velocity) return;
    for (Layer* layer : ctx.scene.layers) {
        if (!layer->visible) continue;
        if (auto* particle = dynamic_cast<ParticleLayer*>(layer)) particle->drawDebug(ctx);
    }
}

void Scene2DRuntime::present() {
    if (scene_output_index < 0 || scene_output_index > 1) return;
    SceneTarget& target = scene_targets[scene_output_index];
    if (target.image.id == SG_INVALID_ID || target.texture_view.id == SG_INVALID_ID) return;

    const bool has_output_viewport = output_width > 0 && output_height > 0;
    const int width = has_output_viewport ? output_width : surface::width();
    const int height = has_output_viewport ? output_height : surface::height();
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, width, height, true);
        sg_apply_scissor_rect(output_x, output_y, width, height, true);
    } else {
        sg_apply_viewport(0, 0, width, height, true);
        sg_apply_scissor_rect(0, 0, width, height, true);
    }

    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, target.image, target.texture_view, 0.0f, 0.0f, (float)width, (float)height,
                         0.0f, white, false, nullptr);

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, surface::width(), surface::height(), true);
        sg_apply_scissor_rect(0, 0, surface::width(), surface::height(), true);
    }
}

void Scene2DRuntime::updateViewport() {
    float sw = output_width > 0 ? (float)output_width : (float)surface::width();
    float sh = output_height > 0 ? (float)output_height : (float)surface::height();
    renderer_update_viewport(&ctx.renderer, sw, sh);

    if (ctx.scene.scene_w == 0 || ctx.scene.scene_h == 0) return;

    float aspect_scene = ctx.scene.scene_w / ctx.scene.scene_h;
    float aspect_window = sw / sh;

    if (ctx.scene.scaling_mode == SCALING_FIT) {
        ctx.scene.render_scale = aspect_window > aspect_scene ? sh / ctx.scene.scene_h : sw / ctx.scene.scene_w;
    } else {
        ctx.scene.render_scale = aspect_window > aspect_scene ? sw / ctx.scene.scene_w : sh / ctx.scene.scene_h;
    }
    // Zoom is part of the authored camera transform, not an editor-only hint.
    ctx.scene.render_scale *= std::max(ctx.scene.general.zoom, 0.001f);

    ctx.scene.offset_x = (sw - ctx.scene.scene_w * ctx.scene.render_scale) * 0.5f;
    ctx.scene.offset_y = (sh - ctx.scene.scene_h * ctx.scene.render_scale) * 0.5f;
}

void Scene2DRuntime::setOutputViewport(int x, int y, int width, int height) {
    output_x = x;
    output_y = y;
    output_width = width;
    output_height = height;
}

void Scene2DRuntime::resetOutputViewport() {
    output_x = 0;
    output_y = 0;
    output_width = 0;
    output_height = 0;
}

void Scene2DRuntime::clearScene() {
    LOG_TAG_I("SCENE_2D", "Destroying %zu scene layers...", ctx.scene.layers.size());
    for (auto layer : ctx.scene.layers) delete layer;
    ctx.scene.layers.clear();
    delete ctx.scene.scene_tree;
    ctx.scene.scene_tree = nullptr;
    ctx.debug.selected_object = -1;
    ctx.debug.test_mode = false;
    LOG_TAG_I("SCENE_2D", "Scene layers destroyed.");
}

void Scene2DRuntime::cleanup() {
    LOG_TAG_I("SCENE_2D", "Cleaning up 2D scene runtime and render targets...");
    clearScene();
    destroyBloomPipelines();
    scene_targets[0].reset();
    scene_targets[1].reset();
    bloom_targets[0].reset();
    bloom_targets[1].reset();
    scene_output_index = -1;
    LOG_TAG_I("SCENE_2D", "2D scene runtime cleanup complete.");
}
