#ifndef TRANSITION_SHADER_H
#define TRANSITION_SHADER_H

#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"
#include "wallpaper/transition/transition_catalog.h"

struct EngineContext;

// What the compositor should play for one switch.
struct TransitionConfig {
    int selection = (int)lwe::transition::Effect::Fade;
    int duration_ms = 1000;
    bool continue_previous = false;
};

// Loads and draws WE's playlist transition shader for one FADEEFFECT combo.
class TransitionShader {
   public:
    // effect_index is a 0..26 FADEEFFECT value; false keeps the built-in fade.
    bool init(EngineContext& ctx, int effect_index);
    void shutdown();
    bool ready() const {
        return shader_.id != SG_INVALID_ID && pipeline_.id != SG_INVALID_ID;
    }

    // Draws the outgoing frame with premultiplied alpha; progress 0 is visible, 1 is gone.
    void drawOldOverNew(EngineContext& ctx, sg_view old_frame, float progress, int width, int height,
                        uint32_t hash_seed);

   private:
    struct DynamicUniforms {
        float progress;
        float hash;
        float hash2;
        float random;
        float aspect;
        float width;
        float height;
        float padding;
        float view_projection[16];
        float view_projection_inv[16];
    };

    GfxShader shader_;
    GfxPipeline pipeline_;
    GfxImage noise_image_;
    GfxView noise_view_;
    GfxImage clouds_image_;
    GfxView clouds_view_;
    GfxBuffer brick_vertices_;
    GfxBuffer brick_indices_;
    GfxBuffer shatter_vertices_;
    GfxBuffer shatter_indices_;
    int shatter_index_count_ = 0;
    int effect_index_ = -1;
};

#endif  // TRANSITION_SHADER_H
