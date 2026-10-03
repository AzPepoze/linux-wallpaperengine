#include "image_layer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "image_parser.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/tree/scene_tree.h"

ImageLayer::ImageLayer(const char* name, GfxImage img) : Layer(name), img(std::move(img)) {}

ImageLayer::~ImageLayer() {
    for (auto& target : effect_targets) {
        target.reset();
    }
    for (auto& [name, rt] : named_effect_targets) {
        rt.reset();
    }
    cached_view = {};
    img = {};
}

void ImageLayer::updateCachedView() {
    if (img.id != SG_INVALID_ID) {
        sg_view_desc v_desc = {};
        v_desc.texture.image = img;
        cached_view = sg_make_view(&v_desc);
    }
}

void ImageLayer::update(float dt, EngineContext& ctx) {
    tint[3] = evaluateImageAlpha(alpha_document, ctx.time);
    if (is_fullscreen || is_compose_region) return;
    if (has_puppet_mesh) {
        puppet_pose.advance(puppet_layers, dt);
        if (renderPuppet(ctx)) {
            renderEffectChain(ctx, (sg_image)puppet_straight.image, (sg_view)puppet_straight.texture_view);
            return;
        }
    }
    renderEffectChain(ctx);
}

void ImageLayer::start() {
    if (bound_video_decoder) bound_video_decoder->start();
}

void ImageLayer::stop() {
    if (bound_video_decoder) bound_video_decoder->stop();
}

void ImageLayer::pause() {
    if (bound_video_decoder) bound_video_decoder->pause();
}

void ImageLayer::resume() {
    if (bound_video_decoder) bound_video_decoder->resume();
}

bool ImageLayer::requiresSceneColor() const {
    return copy_background || color_blend_mode != 0;
}
