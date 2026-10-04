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
};

// Loads and draws Wallpaper Engine's own playlist transition shader
// (assets/shaders/HLSL/dx11playlisttransition.*) for one FADEEFFECT combo.
class TransitionShader {
   public:
    // effect_index is a 0..26 FADEEFFECT value. Returns false (built-in fade
    // stays in charge) when the install has no shader or it fails to compile.
    bool init(EngineContext& ctx, int effect_index);
    void shutdown();
    bool ready() const {
        return shader_.id != SG_INVALID_ID && pipeline_.id != SG_INVALID_ID;
    }

    // Draws the outgoing frame over the currently bound target with
    // premultiplied alpha. progress runs 0 (outgoing visible) .. 1 (gone).
    void drawOldOverNew(EngineContext& ctx, sg_view old_frame, float progress, int width, int height);

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
    int effect_index_ = -1;
};

#endif  // TRANSITION_SHADER_H
