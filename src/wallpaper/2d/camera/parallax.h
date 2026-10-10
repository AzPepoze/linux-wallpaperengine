#ifndef PARALLAX_H
#define PARALLAX_H

#include <stdint.h>

#include "shared/core/engine_context.h"

struct parallax_offset_t {
    float x = 0.0f;
    float y = 0.0f;
};

struct parallax_position_t {
    float x = 0.5f;
    float y = 0.5f;
};

// Copies the scene's parallax settings into ctx, keeping the config.json overrides.
void applyParallaxScene(EngineContext& ctx, const wallpaper_engine::SceneGeneralDocument& general);

void parallax_update(EngineContext& ctx, float dt, int viewport_width, int viewport_height);

parallax_offset_t parallax_layer_offset(const EngineContext& ctx, uint32_t scene_object_id,
                                        const float fallback_origin[3], const float fallback_depth[2]);

parallax_position_t parallax_shader_position(const EngineContext& ctx);

#endif  // PARALLAX_H
