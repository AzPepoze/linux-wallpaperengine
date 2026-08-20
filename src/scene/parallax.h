#ifndef PARALLAX_H
#define PARALLAX_H

#include <stdint.h>

#include "../core/engine_context.h"

struct parallax_offset_t {
    float x = 0.0f;
    float y = 0.0f;
};

struct parallax_position_t {
    float x = 0.5f;
    float y = 0.5f;
};

// Advance the delayed normalized pointer used by Wallpaper Engine camera parallax.
void parallax_update(EngineContext& ctx, float dt, int viewport_width, int viewport_height);

// Resolve normal per-layer camera parallax in scene-space pixels. The scene object
// id lets the resolver apply Wallpaper Engine parent propagation and world transforms;
// origin/depth are retained as a fallback for loose scene JSON without ids.
parallax_offset_t parallax_layer_offset(const EngineContext& ctx, uint32_t scene_object_id,
                                        const float fallback_origin[3], const float fallback_depth[2]);

// Resolve Wallpaper Engine's g_ParallaxPosition builtin.
parallax_position_t parallax_shader_position(const EngineContext& ctx);

#endif  // PARALLAX_H