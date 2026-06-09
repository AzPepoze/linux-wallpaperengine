#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "../../libs/linmath.h"
#include "../../libs/sokol/sokol_gfx.h"

#ifdef __cplusplus
class ShaderPass;
extern "C" {
#else
typedef struct ShaderPass ShaderPass;
#endif

typedef struct {
    float x, y;
    float u, v;
} vertex_t;

typedef struct {
    sg_pipeline pip_alpha;
    sg_pipeline pip_add;
    sg_pipeline pip_lines;
    sg_bindings bind;
    sg_sampler smp;
    sg_image white_pixel;
    float view_width;
    float view_height;
} renderer_t;

void renderer_init(renderer_t* r, float w, float h);
void renderer_update_viewport(renderer_t* r, float w, float h);
typedef struct {
    mat4x4 mvp;
    vec4 texture_resolutions[5];  // 0: main image, 1-4: effect textures
    vec2 parallax_pos;            // 0..1
    float time;
    float padding;
    vec2 screen_res;
    vec2 padding2;
    mat4x4 effect_texture_projection;
    mat4x4 effect_texture_projection_inverse;
} builtin_uniforms_t;

void renderer_draw_sprite(renderer_t* r, sg_image img, float x, float y, float w, float h, float rotation,
                          float tint[4], bool additive, ShaderPass* pass);
void renderer_draw_rect(renderer_t* r, float x, float y, float w, float h, float color[4]);
void renderer_draw_line(renderer_t* r, float x0, float y0, float x1, float y1, float color[4]);
void renderer_cleanup(renderer_t* r);

#ifdef __cplusplus
}
#endif

#endif  // RENDER_H
