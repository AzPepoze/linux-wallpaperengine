#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
#include <stdint.h>

#include <chrono>
#include <memory>
#include <vector>

#include "linmath.h"
#include "shared/core/vfs.h"
#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"

#ifdef __cplusplus
struct EngineContext;
class AssetManager;
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
    bool repeat_effect_input;
    // Bit n marks texture slot n + 1 as defaulting to white when nothing is bound (shader `"default":"util/white"`).
    unsigned int white_default_slots;
    // Render targets and the screen size seen by this pass are reported this many times larger (0 or 1 = actual size).
    float logical_scale;
} render_effect_pass_t;

typedef struct {
    float x, y;
    float u, v;
} vertex_t;

struct renderer_t {
    GfxPipeline pip_alpha;
    GfxPipeline pip_copy;
    GfxPipeline pip_add;
    GfxPipeline pip_unpremul;
    GfxPipeline pip_present;
    GfxPipeline pip_lines;
    GfxPipeline pip_mesh;
    GfxPipeline pip_image_composite[31];
    GfxShader shd_image_composite[31];
    GfxBuffer vertex_buffer;
    GfxBuffer fullscreen_vertex_buffer;
    GfxBuffer index_buffer;
    sg_bindings bind = {};
    GfxSampler smp_repeat;
    GfxSampler smp_clamp;
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
    vec2 parallax_pos;
    float time;
    float frametime;
    vec2 screen_res;
    vec2 texel_size;
    mat4x4 effect_texture_projection;
    mat4x4 effect_texture_projection_inverse;
    vec2 pointer_position;
    vec2 pointer_position_last;
    vec4 pointer_state;
    vec4 light_ambient_color;
    vec4 light_skylight_color;
} builtin_uniforms_t;

typedef struct {
    mat4x4 model_matrix;
    mat4x4 model_matrix_inverse;
    mat4x4 view_projection_matrix;
    vec4 orientation_up;
    vec4 orientation_right;
    vec4 orientation_forward;
    vec4 view_up;
    vec4 view_right;
    vec4 eye_position;
    vec4 render_var0;
    vec4 render_var1;
} particle_builtin_uniforms_t;

#ifdef __cplusplus
static_assert(offsetof(builtin_uniforms_t, pointer_position) % 16 == 0,
              "g_PointerPosition must start at a std140 16-byte slot");
static_assert(sizeof(builtin_uniforms_t) % 16 == 0, "built-in uniform block must preserve std140 tail alignment");
static_assert(sizeof(particle_builtin_uniforms_t) % 16 == 0,
              "particle built-in uniform block must preserve std140 alignment");
#endif

#ifdef __cplusplus
void renderer_draw_sprite(EngineContext& ctx, renderer_t* r, sg_image img, sg_view main_view, float x, float y, float w,
                          float h, float rotation, float tint[4], bool additive, const render_effect_pass_t* pass,
                          bool replace = false, sg_buffer quad_buffer = {});
void renderer_draw_particle_batch(EngineContext& ctx, renderer_t* r, sg_buffer vertex_buffer, sg_buffer index_buffer,
                                  int index_count, sg_image main_image, sg_view main_view,
                                  const render_effect_pass_t* pass, const builtin_uniforms_t& builtins,
                                  const particle_builtin_uniforms_t& particle_builtins);
void renderer_draw_unpremultiplied(renderer_t* r, sg_view source_view, float width, float height);

// Highlight roll-off keeps additive effects from hard-clipping to flat white.
void renderer_present(renderer_t* r, sg_view source_view, float width, float height);

void renderer_draw_mesh(EngineContext& ctx, renderer_t* r, sg_buffer position_buffer, sg_buffer uv_buffer,
                        sg_buffer index_buffer, int index_count, sg_image image, sg_view main_view, const float tint[4],
                        float width, float height);
void renderer_draw_image_composite(EngineContext& ctx, renderer_t* r, sg_image image, sg_view image_view,
                                   sg_view scene_view, float x, float y, float width, float height, float rotation,
                                   float tint[4], int blend_mode);
// Precompiles all blend pipelines during init; creating them mid-render caused GPU context loss.
void renderer_precompile_blend_pipelines(EngineContext& ctx, renderer_t* r, const std::vector<int>& modes = {});

struct BlendPipelinePrecompileJob;
using BlendPipelinePrecompileJobHandle = std::shared_ptr<BlendPipelinePrecompileJob>;
BlendPipelinePrecompileJobHandle renderer_begin_blend_pipeline_precompile(const AssetManager& assets,
                                                                          vfs::PackageHandle package, renderer_t* r,
                                                                          const std::vector<int>& modes = {});
// True once every requested mode is consumed; finalizes at most one completed mode per call.
bool renderer_poll_blend_pipeline_precompile(const BlendPipelinePrecompileJobHandle& job, renderer_t* r,
                                             std::chrono::steady_clock::time_point deadline);
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
