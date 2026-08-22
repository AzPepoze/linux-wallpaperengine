#include "parallax.h"

#include <algorithm>
#include <cmath>

#include "wallpaper/scene/tree/scene_tree.h"

namespace {

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float smooth(float value) {
    return value * value * (3.0f - 2.0f * value);
}

void camera_shake_update(EngineContext& ctx) {
    ctx.camera_shake_x = 0.0f;
    ctx.camera_shake_y = 0.0f;
    if (!ctx.camera_shake_enabled || ctx.camera_shake_amplitude <= 0.0f || ctx.camera_shake_speed <= 0.0f) return;

    const float roughness = std::max(0.0f, std::min(2.0f, ctx.camera_shake_roughness));
    const float grow = std::max(0.0f, roughness - 1.0f);
    const float grow_squared = grow * grow;
    constexpr float pi = 3.14159265358979323846f;
    const float beat_position = std::max(0.0f, ctx.time * ctx.camera_shake_speed * 2.0f) / (pi * 0.5f);
    const int beat = (int)floorf(beat_position);
    const float local = beat_position - (float)beat;

    const float directions[8][2] = {{-1, 1}, {1, -1}, {-1, 1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, -1}};
    const float base_factors[8] = {0.8f, 1.0f, 0.45f, 0.6f, 0.8f, 1.0f, 0.45f, 0.6f};
    const float rough_factors[8] = {6.0f, 8.0f, 1.0f, 1.0f, 6.0f, 8.0f, 1.0f, 1.0f};
    auto sample = [&](int index, float& x, float& y) {
        x = y = 0.0f;
        if ((index & 1) != 0) return;
        const int direction = (index / 2) % 8;
        const float factor = base_factors[direction] * (1.0f + (rough_factors[direction] - 1.0f) * grow_squared);
        x = directions[direction][0] * factor;
        y = directions[direction][1] * factor;
    };
    float ax, ay, bx, by;
    sample(beat, ax, ay);
    sample(beat + 1, bx, by);
    const float dx = bx - ax;
    const float dy = by - ay;
    const float delta_length = sqrtf(dx * dx + dy * dy);
    float curve_x = 0.0f, curve_y = 0.0f;
    if (delta_length > 0.0f) {
        curve_x = -dy / delta_length;
        curve_y = dx / delta_length;
    }
    const float amount = smooth(local);
    const float bend = sinf(local * pi) * (0.09f + grow_squared * 0.04f) * delta_length;
    const float scale = ctx.camera_shake_amplitude * std::min(ctx.scene_w, ctx.scene_h) * 0.01f;
    ctx.camera_shake_x = (ax * (1.0f - amount) + bx * amount + curve_x * bend) * scale;
    ctx.camera_shake_y = (ay * (1.0f - amount) + by * amount + curve_y * bend) * scale;
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
    camera_shake_update(ctx);
}

parallax_offset_t parallax_layer_offset(const EngineContext& ctx, uint32_t scene_object_id,
                                        const float fallback_origin[3], const float fallback_depth[2]) {
    parallax_offset_t result = {};
    result.x = ctx.camera_shake_x;
    result.y = ctx.camera_shake_y;
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
