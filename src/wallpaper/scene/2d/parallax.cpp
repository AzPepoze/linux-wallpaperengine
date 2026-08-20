#include "parallax.h"

#include <algorithm>

#include "wallpaper/scene/tree/scene_tree.h"

namespace {

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

}  // namespace

void parallax_update(EngineContext& ctx, float dt, int viewport_width, int viewport_height) {
    float target_x = 0.5f;
    float target_y = 0.5f;

    if (ctx.camera_parallax_enabled && ctx.mouse_position_valid && viewport_width > 0 && viewport_height > 0) {
        target_x = clamp01(ctx.mouse_x / (float)viewport_width);
        target_y = clamp01(ctx.mouse_y / (float)viewport_height);
    }

    float response = 1.0f;
    if (ctx.camera_parallax_delay > 0.0f) {
        response = dt > 0.0f ? clamp01(dt / ctx.camera_parallax_delay) : 0.0f;
    }

    ctx.parallax_pointer_x += (target_x - ctx.parallax_pointer_x) * response;
    ctx.parallax_pointer_y += (target_y - ctx.parallax_pointer_y) * response;

    const parallax_position_t shader_position = parallax_shader_position(ctx);
    ctx.parallax_smooth_x = (shader_position.x - 0.5f) * 2.0f;
    ctx.parallax_smooth_y = (shader_position.y - 0.5f) * 2.0f;
}

parallax_offset_t parallax_layer_offset(const EngineContext& ctx, uint32_t scene_object_id,
                                        const float fallback_origin[3], const float fallback_depth[2]) {
    parallax_offset_t result = {};
    if (!ctx.camera_parallax_enabled || ctx.camera_parallax_amount == 0.0f) return result;
    if (ctx.scene_w <= 0.0f || ctx.scene_h <= 0.0f) return result;

    float node_position[3] = {fallback_origin[0], fallback_origin[1], fallback_origin[2]};
    const float* depth = fallback_depth;

    if (ctx.scene_tree) {
        if (const SceneTreeNode* resolved = ctx.scene_tree->resolveParallaxNode(scene_object_id)) {
            depth = resolved->parallax_depth.data();
            ctx.scene_tree->worldPosition(resolved->id, node_position);
        }
    }

    if (depth[0] == 0.0f && depth[1] == 0.0f) return result;

    const float camera_x = ctx.scene_w * 0.5f;
    const float camera_y = ctx.scene_h * 0.5f;

    const float mouse_x = (0.5f - ctx.parallax_pointer_x) * ctx.scene_w * ctx.camera_parallax_mouse_influence;
    const float mouse_y = (ctx.parallax_pointer_y - 0.5f) * ctx.scene_h * ctx.camera_parallax_mouse_influence;

    result.x = (node_position[0] - camera_x + mouse_x) * depth[0] * ctx.camera_parallax_amount;
    result.y = (node_position[1] - camera_y + mouse_y) * depth[1] * ctx.camera_parallax_amount;
    return result;
}

parallax_position_t parallax_shader_position(const EngineContext& ctx) {
    parallax_position_t result = {};
    if (!ctx.camera_parallax_enabled) return result;

    const float centered_x = ctx.parallax_pointer_x - 0.5f;
    const float centered_y = ctx.parallax_pointer_y - 0.5f;

    result.x = 0.5f + centered_x * ctx.camera_parallax_mouse_influence;
    result.y = 0.5f - centered_y * ctx.camera_parallax_mouse_influence;
    return result;
}
