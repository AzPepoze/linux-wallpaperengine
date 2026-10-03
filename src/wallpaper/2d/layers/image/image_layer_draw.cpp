#include <algorithm>
#include <cmath>

#include "image_layer.h"
#include "shared/core/context.h"
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
        if (const SceneTreeNode* node = ctx.scene.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
            // Scene space is Y-up while sprites are drawn in Y-down screen space,
            // so the authored Z angle is negated here (same as the attachment path).
            rect.rotation = -node->angles[2];
            if (!node->attachment.empty()) {
                mat4x4 world;
                if (ctx.scene.scene_tree->worldTransform(scene_object_id, world)) {
                    layer_scale[0] = std::hypot(world[0][0], world[0][1]);
                    layer_scale[1] = std::hypot(world[1][0], world[1][1]);
                    // Scene/bone space is Y-up; sprite rotation is in screen space.
                    rect.rotation = -std::atan2(world[0][1], world[0][0]) * 180.0f / (float)M_PI;
                }
            }
        }
        ctx.scene.scene_tree->worldPosition(scene_object_id, layer_origin);
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
    renderer_draw_sprite(ctx, &ctx.renderer, draw_image, draw_view, rect.x, rect.y, rect.width, rect.height,
                         rect.rotation, tint, false, nullptr);
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

void ImageLayer::drawDebug(EngineContext& ctx) {
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    if (scene_object_id != 0 && ctx.scene.scene_tree) {
        if (const SceneTreeNode* node = ctx.scene.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
        }
        ctx.scene.scene_tree->worldPosition(scene_object_id, layer_origin);
    }
    const float width = size[0] * layer_scale[0] * ctx.scene.render_scale;
    const float height = size[1] * layer_scale[1] * ctx.scene.render_scale;
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    const float x = ctx.scene.offset_x + (layer_origin[0] + camera_offset.x) * ctx.scene.render_scale - width * 0.5f;
    const float y = ctx.scene.offset_y + (layer_origin[1] + camera_offset.y) * ctx.scene.render_scale - height * 0.5f;
    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&ctx.renderer, x, y, width, height, color);
}
