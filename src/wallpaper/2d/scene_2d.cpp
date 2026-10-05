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
#include "wallpaper/2d/animation_curve.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/script/scene_scripts.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/2d/tree/scene_tree.h"
#include "wallpaper/2d/tree/scene_visibility.h"

void Scene2DRuntime::init() {
    renderer_init(&ctx.renderer, (float)surface::width(), (float)surface::height());
}

void Scene2DRuntime::precompileBlendModes() {
    // Include hidden layers: they may become visible later. The renderer deduplicates modes and reuses pipelines.
    std::vector<int> modes;
    for (const Layer* layer : ctx.scene.layers) {
        if (const auto* image = dynamic_cast<const ImageLayer*>(layer)) modes.push_back(image->color_blend_mode);
    }
    if (modes.empty()) return;
    renderer_precompile_blend_pipelines(ctx, &ctx.renderer, modes);
}

void Scene2DRuntime::update(float dt) {
    ScriptEngine::instance().setActiveScope(ctx.scene.scripts);
    if (ctx.scene.scripts) ctx.scene.scripts->update(dt);
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

    const SceneVisibility visibility(ctx);
    for (const auto* layer : ctx.scene.layers) {
        if (any_solo ? !layer->solo : !visibility.visible(*layer)) continue;
        const auto* particle = dynamic_cast<const ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) return true;
        const auto* image = dynamic_cast<const ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) return true;
    }
    return false;
}

sg_pixel_format Scene2DRuntime::compositionPixelFormat() const {
    // Float composition preserves additive HDR energy (lens flares, glows); the
    // present pass rolls the highlights off instead of clipping them to white.
    return float_composition_available_ ? SG_PIXELFORMAT_RGBA16F : SG_PIXELFORMAT_RGBA8;
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
        // Drivers without a float attachment fall back to the LDR format.
        if (requested_format == SG_PIXELFORMAT_RGBA16F) {
            float_composition_available_ = false;
            if (scene_targets[0].create(width, height, SG_PIXELFORMAT_RGBA8) &&
                scene_targets[1].create(width, height, SG_PIXELFORMAT_RGBA8)) {
                return true;
            }
            scene_targets[0].reset();
            scene_targets[1].reset();
        }
        return false;
    }
    return true;
}

void Scene2DRuntime::draw() {
    if (force_offscreen_ || requiresOffscreenComposition())
        drawOffscreen();
    else {
        scene_output_index = -1;
        drawDirect();
    }
}

sg_view Scene2DRuntime::composedView() const {
    if (scene_output_index < 0 || scene_output_index > 1) return {};
    return scene_targets[scene_output_index].texture_view;
}

sg_image Scene2DRuntime::composedImage() const {
    if (scene_output_index < 0 || scene_output_index > 1) return {};
    return scene_targets[scene_output_index].image;
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
    renderer_present(&ctx.renderer, target.texture_view, (float)width, (float)height);

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, surface::width(), surface::height(), true);
        sg_apply_scissor_rect(0, 0, surface::width(), surface::height(), true);
    }
}

void Scene2DRuntime::updateViewport() {
    float sw = output_width > 0 ? (float)output_width : (float)surface::width();
    float sh = output_height > 0 ? (float)output_height : (float)surface::height();
    // A scene-aspect projection mapped onto the physical viewport stretches both
    // image and particle geometry together, including rotated layers.
    if (ctx.scene.scene_w > 0 && ctx.scene.scene_h > 0 && ctx.scene.scaling_mode == SCALING_STRETCH)
        sw = sh * ctx.scene.scene_w / ctx.scene.scene_h;
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

    // Camera-path entry animation: zoom about the view centre plus a relative origin pan (camera moves opposite to
    // the content).
    const wallpaper_engine::SceneCameraDocument& camera = ctx.scene.camera;
    float pan_x = 0.0f, pan_y = 0.0f;
    if (!camera.zoom_curve.keys.empty()) {
        const auto& curve = camera.zoom_curve;
        ctx.scene.render_scale *=
            std::max(evaluateCurve(curve.keys, curve.fps, curve.length, curve.mode, ctx.time), 0.001f);
    }
    const auto& origin_x = camera.origin_curves[0];
    const auto& origin_y = camera.origin_curves[1];
    if (!origin_x.keys.empty())
        pan_x = -evaluateCurve(origin_x.keys, origin_x.fps, origin_x.length, origin_x.mode, ctx.time);
    if (!origin_y.keys.empty())
        pan_y = evaluateCurve(origin_y.keys, origin_y.fps, origin_y.length, origin_y.mode, ctx.time);

    ctx.scene.offset_x = (sw - ctx.scene.scene_w * ctx.scene.render_scale) * 0.5f + pan_x * ctx.scene.render_scale;
    ctx.scene.offset_y = (sh - ctx.scene.scene_h * ctx.scene.render_scale) * 0.5f + pan_y * ctx.scene.render_scale;
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
    delete ctx.scene.scripts;  // scripts point at layers and tree nodes, so they go first
    ctx.scene.scripts = nullptr;
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
    hdr_bloom_levels.clear();
    scene_output_index = -1;
    force_offscreen_ = false;
    LOG_TAG_I("SCENE_2D", "2D scene runtime cleanup complete.");
}
