#include <algorithm>
#include <cmath>

#include "image_layer.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/render.h"

void ImageLayer::updateAnimatedFrame(EngineContext& ctx) {
    const auto* frame = wallpaper_engine::textureFrameAtTime(texture_metadata, ctx.time);
    if (!frame || frame == current_texture_frame) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();

    sg_image page = img;
    sg_view page_view = cached_view;
    if (frame->image_index != 0) {
        if (animation_page.id == SG_INVALID_ID || animation_page_index != frame->image_index) {
            // Keep only the active extra page; large animated atlases can occupy
            // hundreds of megabytes if every page is uploaded at once.
            animation_page_view = {};
            animation_page = ctx.asset_mgr.resolveTexture(path.c_str(), nullptr, (int)frame->image_index);
            animation_page_index = frame->image_index;
            if (animation_page.id != SG_INVALID_ID) {
                sg_view_desc view_desc = {};
                view_desc.texture.image = animation_page;
                animation_page_view = sg_make_view(&view_desc);
            }
        }
        page = animation_page;
        page_view = animation_page_view;
    }
    if (page.id == SG_INVALID_ID || page_view.id == SG_INVALID_ID) return;

    const int width = std::max(1, (int)std::lround(frame->width));
    const int height = std::max(1, (int)std::lround(frame->height));
    if (animated_frame.width != width || animated_frame.height != height) {
        if (!animated_frame.create(width, height)) return;
    }
    const float saved_width = ctx.renderer.view_width;
    const float saved_height = ctx.renderer.view_height;
    sg_pass pass = {};
    pass.attachments.colors[0] = animated_frame.attachment_view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    sg_begin_pass(&pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // The viewport clips the full atlas down to the authored frame rectangle.
    // Replace blending preserves straight alpha for effects and the final draw.
    renderer_draw_sprite(ctx, &ctx.renderer, page, page_view, -frame->x, -frame->y, (float)texture_metadata.width,
                         (float)texture_metadata.height, 0.0f, white, false, nullptr, true);
    sg_end_pass();
    renderer_update_viewport(&ctx.renderer, saved_width, saved_height);
    current_texture_frame = frame;
}
