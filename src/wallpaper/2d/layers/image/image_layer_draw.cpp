#include "image_layer.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/tree/scene_tree.h"

void ImageLayer::draw(EngineContext& ctx) {
    if (img.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();

    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_rotation = rotation;
    if (scene_object_id != 0 && ctx.scene_tree) {
        if (const SceneTreeNode* node = ctx.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
            layer_rotation = node->angles[2];
        }
        ctx.scene_tree->worldPosition(scene_object_id, layer_origin);
    }

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (is_fullscreen) {
        x = 0.0f;
        y = 0.0f;
        width = ctx.renderer.view_width;
        height = ctx.renderer.view_height;
    } else {
        const float scene_h =
            ctx.scene_h > 0.0f ? ctx.scene_h : (ctx.renderer.view_height > 0.0f ? ctx.renderer.view_height : 2160.0f);
        width = size[0] * layer_scale[0] * ctx.render_scale;
        height = size[1] * layer_scale[1] * ctx.render_scale;
        const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
        x = ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - width * 0.5f;
        y = ctx.offset_y + (scene_h - (layer_origin[1] + camera_offset.y)) * ctx.render_scale - height * 0.5f;
    }

    sg_image draw_image = img;
    sg_view draw_view = cached_view;
    if (effect_output_image.id != SG_INVALID_ID && effect_output_view.id != SG_INVALID_ID) {
        draw_image = effect_output_image;
        draw_view = effect_output_view;
    }
    renderer_draw_sprite(ctx, &ctx.renderer, draw_image, draw_view, x, y, width, height, layer_rotation, tint, false,
                         nullptr);
}

void ImageLayer::drawComposite(EngineContext& ctx, sg_view scene_view) {
    if (img.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();

    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_rotation = rotation;
    if (scene_object_id != 0 && ctx.scene_tree) {
        if (const SceneTreeNode* node = ctx.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
            layer_rotation = node->angles[2];
        }
        ctx.scene_tree->worldPosition(scene_object_id, layer_origin);
    }
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (is_fullscreen) {
        x = 0.0f;
        y = 0.0f;
        width = ctx.renderer.view_width;
        height = ctx.renderer.view_height;
    } else {
        const float scene_h =
            ctx.scene_h > 0.0f ? ctx.scene_h : (ctx.renderer.view_height > 0.0f ? ctx.renderer.view_height : 2160.0f);
        width = size[0] * layer_scale[0] * ctx.render_scale;
        height = size[1] * layer_scale[1] * ctx.render_scale;
        const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
        x = ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - width * 0.5f;
        y = ctx.offset_y + (scene_h - (layer_origin[1] + camera_offset.y)) * ctx.render_scale - height * 0.5f;
    }
    sg_image draw_image = img;
    sg_view draw_view = cached_view;
    if (effect_output_image.id != SG_INVALID_ID && effect_output_view.id != SG_INVALID_ID) {
        draw_image = effect_output_image;
        draw_view = effect_output_view;
    }
    renderer_draw_image_composite(ctx, &ctx.renderer, draw_image, draw_view, scene_view, x, y, width, height,
                                  layer_rotation, tint, color_blend_mode);
}

void ImageLayer::drawDebug(EngineContext& ctx) {
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    if (scene_object_id != 0 && ctx.scene_tree) {
        if (const SceneTreeNode* node = ctx.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
        }
        ctx.scene_tree->worldPosition(scene_object_id, layer_origin);
    }
    const float width = size[0] * layer_scale[0] * ctx.render_scale;
    const float height = size[1] * layer_scale[1] * ctx.render_scale;
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    const float x = ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - width * 0.5f;
    const float y = ctx.offset_y + (layer_origin[1] + camera_offset.y) * ctx.render_scale - height * 0.5f;
    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&ctx.renderer, x, y, width, height, color);
}
