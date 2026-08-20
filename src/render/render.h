#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
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
    const sg_view* override_views;
    size_t num_override_views;
    void (*apply_custom_uniforms)(void* user_data);
    void* user_data;
    bool is_fullscreen_quad;
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
    GfxBuffer fullscreen_vertex_buffer;
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
    vec2 texel_size;
    mat4x4 effect_texture_projection;
    mat4x4 effect_texture_projection_inverse;
    vec2 pointer_position;
    vec2 padding3;
} builtin_uniforms_t;

#ifdef __cplusplus
static_assert(offsetof(builtin_uniforms_t, pointer_position) % 16 == 0,
              "g_PointerPosition must start at a std140 16-byte slot");
static_assert(sizeof(builtin_uniforms_t) % 16 == 0, "built-in uniform block must preserve std140 tail alignment");
#endif

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
