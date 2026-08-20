#include "parallax.h"

#include <algorithm>
#include <math.h>

namespace {
float clamp_unit(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}
}  // namespace

void parallax_update(EngineContext& ctx, float dt, int viewport_width, int viewport_height) {
    float target_x = 0.0f;
    float target_y = 0.0f;

    if (ctx.camera_parallax_enabled && viewport_width > 0 && viewport_height > 0) {
        const float pointer_x = clamp_unit(ctx.mouse_x / (float)viewport_width);
        const float pointer_y = clamp_unit(ctx.mouse_y / (float)viewport_height);

        // Store a centered normalized offset scaled only by mouse influence.
        // Y is inverted to match Wallpaper Engine's bottom-up parallax space.
        target_x = (pointer_x - 0.5f) * 2.0f * ctx.camera_parallax_mouse_influence;
        target_y = -(pointer_y - 0.5f) * 2.0f * ctx.camera_parallax_mouse_influence;
    }

    float response = 1.0f;
    if (ctx.camera_parallax_delay > 0.0f && dt > 0.0f) {
        response = 1.0f - expf(-dt / ctx.camera_parallax_delay);
    }

    ctx.parallax_smooth_x += (target_x - ctx.parallax_smooth_x) * response;
    ctx.parallax_smooth_y += (target_y - ctx.parallax_smooth_y) * response;
}

parallax_offset_t parallax_layer_offset(const EngineContext& ctx, const float depth[2]) {
    parallax_offset_t result = {};
    if (!ctx.camera_parallax_enabled || ctx.camera_parallax_amount == 0.0f) return result;

    // parallax_smooth is twice the centered pointer offset after mouse influence.
    // Convert that normalized camera offset into scene-space translation using
    // the orthographic half extents, then apply the layer's per-axis depth.
    result.x = -0.5f * ctx.parallax_smooth_x * ctx.scene_w * ctx.camera_parallax_amount * depth[0];
    result.y = -0.5f * ctx.parallax_smooth_y * ctx.scene_h * ctx.camera_parallax_amount * depth[1];
    return result;
}
