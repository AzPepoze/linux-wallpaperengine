#include <algorithm>
#include <cmath>

#include "image_layer.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/tree/scene_tree.h"

ImageLayer::ScreenRect ImageLayer::screenRect(EngineContext& ctx) const {
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    ScreenRect rect;
    rect.rotation = rotation;
    if (scene_object_id != 0 && ctx.scene.scene_tree) {
        ScenePlacement placement;
        if (ctx.scene.scene_tree->worldPlacement(scene_object_id, placement)) {
            layer_scale[0] = placement.scale[0];
            layer_scale[1] = placement.scale[1];
            layer_scale[2] = placement.scale[2];
            layer_origin[0] = placement.origin[0];
            layer_origin[1] = placement.origin[1];
            layer_origin[2] = placement.origin[2];
            // Scene space is Y-up and sprites are Y-down, so the Z angle is negated.
            rect.rotation = -placement.rotation_deg;
        }
    }

    if (is_fullscreen) {
        rect.width = ctx.renderer.view_width;
        rect.height = ctx.renderer.view_height;
        return rect;
    }
    const float scene_h = ctx.scene.scene_h > 0.0f
                              ? ctx.scene.scene_h
                              : (ctx.renderer.view_height > 0.0f ? ctx.renderer.view_height : 2160.0f);
    rect.width = size[0] * layer_scale[0] * ctx.scene.render_scale;
    rect.height = size[1] * layer_scale[1] * ctx.scene.render_scale;
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    rect.x = ctx.scene.offset_x + (layer_origin[0] + camera_offset.x) * ctx.scene.render_scale - rect.width * 0.5f;
    rect.y = ctx.scene.offset_y + (scene_h - (layer_origin[1] + camera_offset.y)) * ctx.scene.render_scale -
             rect.height * 0.5f;
    // renderer_draw_sprite pivots on the top-left; keep the image centre on the node.
    const float angle = rect.rotation * (float)M_PI / 180.0f;
    const float half_width = rect.width * 0.5f;
    const float half_height = rect.height * 0.5f;
    rect.x += half_width - (std::cos(angle) * half_width - std::sin(angle) * half_height);
    rect.y += half_height - (std::sin(angle) * half_width + std::cos(angle) * half_height);
    return rect;
}

void ImageLayer::draw(EngineContext& ctx) {
    const bool has_effect_output = effect_output_image.id != SG_INVALID_ID && effect_output_view.id != SG_INVALID_ID;
    if (img.id == SG_INVALID_ID && !has_effect_output) return;
    if (img.id != SG_INVALID_ID && cached_view.id == SG_INVALID_ID) updateCachedView();

    const ScreenRect rect = screenRect(ctx);

    sg_image draw_image = current_texture_frame ? (sg_image)animated_frame.image : (sg_image)img;
    sg_view draw_view = current_texture_frame ? (sg_view)animated_frame.texture_view : (sg_view)cached_view;
    if (puppet_resolved) {
        draw_image = puppet_straight.image;
        draw_view = puppet_straight.texture_view;
    } else if (has_effect_output) {
        draw_image = effect_output_image;
        draw_view = effect_output_view;
    }
    const bool draw_region = has_effect_output && !puppet_resolved && output_region.valid;
    // A cropped source only matches the whole layer; effect output always uses its own quad.
    sg_buffer quad = source_quad;
    if (draw_region) {
        quad = output_quad;
    } else if (has_effect_output || puppet_resolved) {
        quad = {SG_INVALID_ID};
    }
    renderer_draw_sprite(ctx, &ctx.renderer, draw_image, draw_view, rect.x, rect.y, rect.width, rect.height,
                         rect.rotation, tint, false, nullptr, false, quad);
}

void ImageLayer::updateOutputQuad(EngineContext& ctx) {
    if (!output_region.valid) return;
    if (output_quad.id == SG_INVALID_ID) {
        sg_buffer_desc desc = {};
        desc.size = sizeof(vertex_t) * 4;
        desc.usage.vertex_buffer = true;
        desc.usage.stream_update = true;
        output_quad = GfxBuffer(sg_make_buffer(&desc));
        output_quad_region = {};
        if (output_quad.id == SG_INVALID_ID) {
            output_region = {};
            return;
        }
    }
    const content_bounds::Rect& r = output_region;
    const content_bounds::Rect& held = output_quad_region;
    if (held.valid && held.u0 == r.u0 && held.v0 == r.v0 && held.u1 == r.u1 && held.v1 == r.v1) return;

    const uint64_t frame = (uint64_t)ctx.profiler.frame_index;
    if (output_quad_frame == frame) {
        output_region = {};
        return;
    }
    const vertex_t vertices[4] = {
        {r.u0, r.v0, r.u0, r.v0}, {r.u1, r.v0, r.u1, r.v0}, {r.u1, r.v1, r.u1, r.v1}, {r.u0, r.v1, r.u0, r.v1}};
    const sg_range range = SG_RANGE(vertices);
    sg_update_buffer(output_quad, &range);
    output_quad_region = r;
    output_quad_frame = frame;
}

void ImageLayer::drawComposite(EngineContext& ctx, sg_view scene_view) {
    const bool has_effect_output = effect_output_image.id != SG_INVALID_ID && effect_output_view.id != SG_INVALID_ID;
    if (img.id == SG_INVALID_ID && !has_effect_output) return;
    if (img.id != SG_INVALID_ID && cached_view.id == SG_INVALID_ID) updateCachedView();

    const ScreenRect rect = screenRect(ctx);
    sg_image draw_image = current_texture_frame ? (sg_image)animated_frame.image : (sg_image)img;
    sg_view draw_view = current_texture_frame ? (sg_view)animated_frame.texture_view : (sg_view)cached_view;
    if (puppet_resolved) {
        draw_image = puppet_straight.image;
        draw_view = puppet_straight.texture_view;
    } else if (has_effect_output) {
        draw_image = effect_output_image;
        draw_view = effect_output_view;
    }
    renderer_draw_image_composite(ctx, &ctx.renderer, draw_image, draw_view, scene_view, rect.x, rect.y, rect.width,
                                  rect.height, rect.rotation, tint, color_blend_mode);
}

void ImageLayer::renderRegionEffectChain(EngineContext& ctx, sg_image scene_image, sg_view scene_view) {
    const ScreenRect rect = screenRect(ctx);
    const int width = std::max(1, (int)std::lround(rect.width));
    const int height = std::max(1, (int)std::lround(rect.height));
    if (region_source.width != width || region_source.height != height) {
        if (!region_source.create(width, height)) return;
    }

    const float saved_view_width = ctx.renderer.view_width;
    const float saved_view_height = ctx.renderer.view_height;
    sg_pass crop_pass = {};
    crop_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    crop_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    crop_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    crop_pass.attachments.colors[0] = region_source.attachment_view;
    sg_begin_pass(&crop_pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, scene_image, scene_view, -rect.x, -rect.y, saved_view_width,
                         saved_view_height, 0.0f, white, false, nullptr);
    sg_end_pass();
    renderer_update_viewport(&ctx.renderer, saved_view_width, saved_view_height);

    renderEffectChain(ctx, region_source.image, region_source.texture_view);
}

std::array<float, 8> ImageLayer::screenCorners(EngineContext& ctx) const {
    const ScreenRect rect = screenRect(ctx);
    const float angle = rect.rotation * (float)M_PI / 180.0f;
    const float c = std::cos(angle), s = std::sin(angle);
    std::array<float, 8> points;
    const float x[4] = {0.0f, rect.width, rect.width, 0.0f};
    const float y[4] = {0.0f, 0.0f, rect.height, rect.height};
    for (int i = 0; i < 4; ++i) {
        points[i * 2] = rect.x + c * x[i] - s * y[i];
        points[i * 2 + 1] = rect.y + s * x[i] + c * y[i];
    }
    return points;
}

void ImageLayer::drawDebug(EngineContext& ctx) {
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    if (scene_object_id != 0 && ctx.scene.scene_tree) {
        ScenePlacement placement;
        if (ctx.scene.scene_tree->worldPlacement(scene_object_id, placement)) {
            layer_scale[0] = placement.scale[0];
            layer_scale[1] = placement.scale[1];
            layer_scale[2] = placement.scale[2];
            layer_origin[0] = placement.origin[0];
            layer_origin[1] = placement.origin[1];
            layer_origin[2] = placement.origin[2];
        }
    }
    const float width = size[0] * layer_scale[0] * ctx.scene.render_scale;
    const float height = size[1] * layer_scale[1] * ctx.scene.render_scale;
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    const float x = ctx.scene.offset_x + (layer_origin[0] + camera_offset.x) * ctx.scene.render_scale - width * 0.5f;
    const float y = ctx.scene.offset_y + (layer_origin[1] + camera_offset.y) * ctx.scene.render_scale - height * 0.5f;
    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&ctx.renderer, x, y, width, height, color);
}
