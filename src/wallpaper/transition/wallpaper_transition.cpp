#include "wallpaper/transition/wallpaper_transition.h"

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"
#include "shared/graphics/render.h"

bool WallpaperTransition::ensureTarget(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (image_.id != SG_INVALID_ID && width_ == width && height_ == height && texture_view_.id != SG_INVALID_ID &&
        attachment_view_.id != SG_INVALID_ID) {
        return true;
    }

    destroyTarget();
    sg_image_desc image_desc = {};
    image_desc.usage.color_attachment = true;
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    image_ = sg_make_image(&image_desc);
    if (image_.id == SG_INVALID_ID) return false;

    sg_view_desc texture_desc = {};
    texture_desc.texture.image = image_;
    texture_view_ = sg_make_view(&texture_desc);

    sg_view_desc attachment_desc = {};
    attachment_desc.color_attachment.image = image_;
    attachment_view_ = sg_make_view(&attachment_desc);

    if (texture_view_.id == SG_INVALID_ID || attachment_view_.id == SG_INVALID_ID) {
        destroyTarget();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void WallpaperTransition::destroyTarget() {
    attachment_view_ = GfxView();
    texture_view_ = GfxView();
    image_ = GfxImage();
    width_ = 0;
    height_ = 0;
}

bool WallpaperTransition::begin(EngineContext& ctx, sg_view source, sg_image source_image, int width, int height,
                                const TransitionConfig& config) {
    if (source.id == SG_INVALID_ID) return false;
    if (!ensureTarget(width, height)) {
        LOG_TAG_W("TRANSITION", "Could not allocate a %dx%d snapshot; switching without a transition", width, height);
        return false;
    }

    config_ = config;
    elapsed_ = 0.0f;
    progress_ = 0.0f;
    hold_ = false;

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
    pass.attachments.colors[0] = attachment_view_;
    sg_begin_pass(&pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_scissor_rect(0, 0, width, height, true);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, source_image, source, 0.0f, 0.0f, (float)width, (float)height, 0.0f, white,
                         false, nullptr, /*replace=*/true);
    sg_end_pass();

    // Load the matching Wallpaper Engine transition shader; if the install
    // lacks it the built-in fade in composite() takes over.
    if (config.selection >= 0) shader_.init(ctx, config.selection);

    active_ = true;
    return true;
}

void WallpaperTransition::update(float dt) {
    if (!active_ || hold_) return;
    elapsed_ += dt;
    progress_ = lwe::transition::transitionProgress(elapsed_, config_.duration_ms);
    if (progress_ >= 1.0f) active_ = false;
}

void WallpaperTransition::composite(EngineContext& ctx) {
    if (!active_ || texture_view_.id == SG_INVALID_ID) return;

    const int width = surface::width();
    const int height = surface::height();
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_scissor_rect(0, 0, width, height, true);

    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f - progress_};
    if (shader_.ready()) {
        shader_.drawOldOverNew(ctx, texture_view_, progress_, width, height);
        return;
    }
    renderer_draw_sprite(ctx, &ctx.renderer, image_, texture_view_, 0.0f, 0.0f, (float)width, (float)height, 0.0f, tint,
                         false, nullptr);
}

void WallpaperTransition::cancel() {
    active_ = false;
    progress_ = 1.0f;
}

void WallpaperTransition::shutdown() {
    active_ = false;
    shader_.shutdown();
    destroyTarget();
}
