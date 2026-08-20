#ifndef PARALLAX_H
#define PARALLAX_H

#include "../core/engine_context.h"

struct parallax_offset_t {
    float x = 0.0f;
    float y = 0.0f;
};

// Update the shared camera-parallax pointer state used by both normal layer
// parallax and the g_ParallaxPosition shader builtin.
void parallax_update(EngineContext& ctx, float dt, int viewport_width, int viewport_height);

// Resolve a layer's normal camera-parallax translation in scene-space pixels.
parallax_offset_t parallax_layer_offset(const EngineContext& ctx, const float depth[2]);

#endif  // PARALLAX_H
