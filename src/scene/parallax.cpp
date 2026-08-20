#include "parallax.h"

#include <algorithm>
#include <math.h>

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
    if (ctx.camera_parallax_delay > 0.0f && dt > 0.0f) {
        response = 1.0f - expf(-dt / ctx.camera_parallax_delay);
    }

    ctx.parallax_pointer_x += (target_x - ctx.parallax_pointer_x) * response;
    ctx.parallax_pointer_y += (target_y - ctx.parallax_pointer_y) * response;
}

parallax_offset_t parallax_layer_offset(const EngineContext& ctx, const float origin[3], const float depth[2]) {
    parallax_offset_t result = {};
    if (!ctx.camera_parallax_enabled || ctx.camera_parallax_amount == 0.0f) return result;
    if ((depth[0] == 0.0f && depth[1] == 0.0f) || ctx.scene_w <= 0.0f || ctx.scene_h <= 0.0f) return result;

    // Wallpaper Engine's orthographic camera is centered on half of the scene extent.
    const float camera_x = ctx.scene_w * 0.5f;
    const float camera_y = ctx.scene_h * 0.5f;

    // Translate cursor motion into scene-space camera motion. In our top-left screen
    // coordinate system, a cursor move to the right/down moves positive-depth layers
    // left/up, matching camera parallax rather than dragging the layer with the cursor.
    const float mouse_x = (0.5f - ctx.parallax_pointer_x) * ctx.scene_w * ctx.camera_parallax_mouse_influence;
    const float mouse_y = (0.5f - ctx.parallax_pointer_y) * ctx.scene_h * ctx.camera_parallax_mouse_influence;

    result.x = (origin[0] - camera_x + mouse_x) * depth[0] * ctx.camera_parallax_amount;
    result.y = (origin[1] - camera_y + mouse_y) * depth[1] * ctx.camera_parallax_amount;
    return result;
}

parallax_position_t parallax_shader_position(const EngineContext& ctx) {
    parallax_position_t result = {};
    if (!ctx.camera_parallax_enabled) return result;

    const float centered_x = ctx.parallax_pointer_x - 0.5f;
    const float centered_y = ctx.parallax_pointer_y - 0.5f;

    // g_ParallaxPosition is a normalized shader input and intentionally does not
    // include camera_parallax_amount. Wallpaper Engine applies only mouse influence
    // here and flips Y into shader space.
    result.x = 0.5f + centered_x * ctx.camera_parallax_mouse_influence;
    result.y = 0.5f - centered_y * ctx.camera_parallax_mouse_influence;
    return result;
}
