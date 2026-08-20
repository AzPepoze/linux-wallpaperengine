#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "../../libs/linmath.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../core/gfx_resource.h"

#ifdef __cplusplus
struct EngineContext;
extern "C" {
#else
typedef struct EngineContext EngineContext;
#endif

typedef struct {
    bool enabled;
    sg_pipeline pipeline;
    const char* shader_name;
    const GfxView* extra_views;
    size_t num_extra_views;
    void (*apply_custom_uniforms)(void* user_data);
    void* user_data;
} render_effect_pass_t;

typedef struct {
    float x, y;
    float u, v;
} vertex_t;

struct renderer_t {
    GfxPipeline pip_alpha;
    GfxPipeline pip_add;
    GfxPipeline pip_lines;
    GfxBuffer vertex_buffer;
    GfxBuffer index_buffer;
    sg_bindings bind = {};
    GfxSampler smp;
    GfxImage white_pixel;
    GfxView white_view;
    GfxImage black_pixel;
    GfxView black_view;
    GfxImage gray_pixel;
    GfxView gray_view;
    float view_width = 0.0f;
    float view_height = 0.0f;
    uint32_t draw_calls = 0;
};

void renderer_init(renderer_t* r, float w, float h);
void renderer_update_viewport(renderer_t* r, float w, float h);
typedef struct {
    mat4x4 mvp;
    mat4x4 mvp_inverse;
    vec4 texture_resolutions[5];  // 0: main image, 1-4: effect textures
    vec2 parallax_pos;            // 0..1
    float time;
    float padding;
    vec2 screen_res;
    vec2 padding2;
    mat4x4 effect_texture_projection;
    mat4x4 effect_texture_projection_inverse;
} builtin_uniforms_t;

#ifdef __cplusplus
void renderer_draw_sprite(EngineContext& ctx, renderer_t* r, sg_image img, sg_view main_view, float x, float y, float w,
                          float h, float rotation, float tint[4], bool additive, const render_effect_pass_t* pass);
#else
void renderer_draw_sprite(EngineContext* ctx, renderer_t* r, sg_image img, sg_view main_view, float x, float y, float w,
                          float h, float rotation, float tint[4], bool additive, const render_effect_pass_t* pass);
#endif

void renderer_draw_rect(renderer_t* r, float x, float y, float w, float h, float color[4]);
void renderer_draw_line(renderer_t* r, float x0, float y0, float x1, float y1, float color[4]);
void renderer_cleanup(renderer_t* r);

#ifdef __cplusplus
}
#endif

#endif  // RENDER_H
