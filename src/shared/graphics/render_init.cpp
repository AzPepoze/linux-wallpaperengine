#include <math.h>

#include <future>
#include <string>
#include <vector>

#include "render.h"
#include "render_internal.h"
#include "shader/shader_backend.h"
#include "shader/shader_compiler.h"
#include "shader/shader_processor.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "sokol_glue.h"

namespace {
using render_internal::kFirstWallpaperBlendMode;
using render_internal::kLastWallpaperBlendMode;

// GLSL sources for one blend mode, prepared on worker threads and consumed on the main thread.
struct BlendShaderSources {
    int mode = 0;
    std::string vert;
    std::string frag;
    bool valid = false;
};

// Thread-safe: only reads asset files and does string processing — no GPU calls.
BlendShaderSources prepareBlendShaderSources(EngineContext& ctx, int blend_mode) {
    BlendShaderSources result;
    result.mode = blend_mode;

    const std::string vertex_source =
        "#version 330\n"
        "uniform mat4 g_ModelViewProjectionMatrix;\n"
        "uniform mat4 g_ModelViewProjectionMatrixInverse;\n"
        "uniform vec4 g_Texture0Resolution;\n"
        "uniform vec4 g_Texture1Resolution;\n"
        "uniform vec4 g_Texture2Resolution;\n"
        "uniform vec4 g_Texture3Resolution;\n"
        "uniform vec4 g_Texture4Resolution;\n"
        "uniform vec2 g_ParallaxPosition;\n"
        "uniform float g_Time;\n"
        "uniform vec2 g_Screen;\n"
        "uniform vec2 g_TexelSize;\n"
        "uniform mat4 g_EffectTextureProjectionMatrix;\n"
        "uniform mat4 g_EffectTextureProjectionMatrixInverse;\n"
        "uniform vec2 g_PointerPosition;\n"
        "uniform vec4 g_LightAmbientColor;\n"
        "uniform vec4 g_LightSkylightColor;\n"
        "layout(location=0) in vec2 a_Position;\n"
        "layout(location=1) in vec2 a_TexCoord;\n"
        "out vec2 v_TexCoord;\n"
        "out vec2 v_SceneUV;\n"
        "void main() {\n"
        "    gl_Position = g_ModelViewProjectionMatrix * vec4(a_Position, 0.0, 1.0);\n"
        "    v_TexCoord = a_TexCoord;\n"
        "    v_SceneUV = vec2(gl_Position.x * 0.5 + 0.5, 0.5 - gl_Position.y * 0.5);\n"
        "}\n";

    const std::string fragment_source =
        "#version 330\n"
        "precision mediump float;\n"
        "#include \"common_blending.h\"\n"
        "uniform mat4 g_ModelViewProjectionMatrixInverse;\n"
        "uniform vec4 g_Texture0Resolution;\n"
        "uniform vec4 g_Texture1Resolution;\n"
        "uniform vec4 g_Texture2Resolution;\n"
        "uniform vec4 g_Texture3Resolution;\n"
        "uniform vec4 g_Texture4Resolution;\n"
        "uniform vec2 g_ParallaxPosition;\n"
        "uniform float g_Time;\n"
        "uniform vec2 g_Screen;\n"
        "uniform vec2 g_TexelSize;\n"
        "uniform mat4 g_EffectTextureProjectionMatrix;\n"
        "uniform mat4 g_EffectTextureProjectionMatrixInverse;\n"
        "uniform vec2 g_PointerPosition;\n"
        "uniform vec4 g_LightAmbientColor;\n"
        "uniform vec4 g_LightSkylightColor;\n"
        "uniform vec4 tint;\n"
        "uniform sampler2D g_Texture0;\n"
        "uniform sampler2D g_Texture1;\n"
        "in vec2 v_TexCoord;\n"
        "in vec2 v_SceneUV;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "    vec4 source = texture(g_Texture0, v_TexCoord) * tint;\n"
        "    vec4 background = texture(g_Texture1, v_SceneUV);\n"
        "    frag_color = vec4(ApplyBlending(BLENDMODE, background.rgb, source.rgb, source.a), 1.0);\n"
        "}\n";

    std::string processed_vert = ShaderSourceProcessor::processShaderSource(
        vertex_source, "shaders/linux-wallpaperengine/image_composite.vert", ctx.asset_mgr, true);
    std::string processed_frag = ShaderSourceProcessor::processShaderSource(
        fragment_source, "shaders/linux-wallpaperengine/image_composite.frag", ctx.asset_mgr, false);

    // Do not fall back to an approximate implementation when the authoritative WE header is missing.
    if (processed_frag.find("#include \"common_blending.h\"") != std::string::npos) {
        LOG_TAG_E("RENDER", "Required Wallpaper Engine common_blending.h was not found for blend mode %d", blend_mode);
        return result;
    }

    const std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    const std::string blend_define = "#define BLENDMODE " + std::to_string(blend_mode) + "\n";
    result.vert = prefix + processed_vert;
    result.frag = prefix + blend_define + processed_frag;
    result.valid = true;
    return result;
}

// The pipeline borrows the shader's layouts, so the shader must outlive it (sokol frees them a few frames after
// sg_destroy_shader).
bool finalizeBlendPipeline(const BlendShaderSources& sources, GfxShader& shader_out, GfxPipeline& pipeline_out) {
    CompiledShader shader =
        ShaderCompiler::compile("image-composite-" + std::to_string(sources.mode), sources.vert, sources.frag, {}, 1);
    if (shader.pipeline.id == SG_INVALID_ID) {
        LOG_TAG_E("RENDER", "Required Wallpaper Engine blend mode %d failed to compile", sources.mode);
        return false;
    }
    shader_out = std::move(shader.shader);
    pipeline_out = std::move(shader.pipeline);
    return true;
}
}  // namespace

void renderer_init(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;

    vertex_t vertices[] = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    sg_buffer_desc v_desc = {};
    v_desc.data = SG_RANGE(vertices);
    r->vertex_buffer = sg_make_buffer(&v_desc);
    r->bind.vertex_buffers[0] = r->vertex_buffer;

    vertex_t fullscreen_vertices[] = {
        {-1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 0.0f, 1.0f}};
    sg_buffer_desc fsv_desc = {};
    fsv_desc.data = SG_RANGE(fullscreen_vertices);
    r->fullscreen_vertex_buffer = sg_make_buffer(&fsv_desc);

    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    sg_buffer_desc i_desc = {};
    i_desc.usage.index_buffer = true;
    i_desc.data = SG_RANGE(indices);
    r->index_buffer = sg_make_buffer(&i_desc);
    r->bind.index_buffer = r->index_buffer;

    sg_sampler_desc s_desc = {};
    s_desc.min_filter = SG_FILTER_LINEAR;
    s_desc.mag_filter = SG_FILTER_LINEAR;
    s_desc.wrap_u = SG_WRAP_REPEAT;
    s_desc.wrap_v = SG_WRAP_REPEAT;
    r->smp_repeat = sg_make_sampler(&s_desc);
    s_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    s_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    r->smp_clamp = sg_make_sampler(&s_desc);
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; i++) {
        r->bind.samplers[i] = r->smp_repeat;
    }

    uint32_t pixel = 0xFFFFFFFF;
    sg_image_desc img_desc = {};
    img_desc.width = 1;
    img_desc.height = 1;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {&pixel, 4};
    r->white_pixel = sg_make_image(&img_desc);

    sg_view_desc wv_desc = {};
    wv_desc.texture.image = r->white_pixel;
    r->white_view = sg_make_view(&wv_desc);

    pixel = 0x00000000;
    r->black_pixel = sg_make_image(&img_desc);

    sg_view_desc bv_desc = {};
    bv_desc.texture.image = r->black_pixel;
    r->black_view = sg_make_view(&bv_desc);

    pixel = 0x808080FF;  // Retained as a general-purpose neutral gray fallback/debug texture.
    r->gray_pixel = sg_make_image(&img_desc);

    sg_view_desc gv_desc = {};
    gv_desc.texture.image = r->gray_pixel;
    r->gray_view = sg_make_view(&gv_desc);

    const std::string vertex_source =
        "#version 330\n"
        "uniform mat4 mvp;\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 texcoord0;\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "  gl_Position = mvp * vec4(position, 0.0, 1.0);\n"
        "  uv = texcoord0;\n"
        "}\n";
    const std::string fragment_source =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D tex;\n"
        "uniform vec4 tint;\n"
        "in vec2 uv;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  frag_color = texture(tex, uv) * tint;\n"
        "}\n";

    sg_shader_desc shd_desc = {};
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(float) * 4;
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "tint";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].glsl_name = "tex";
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;

    sg_shader shd = create_backend_shader(&shd_desc, vertex_source, fragment_source, "renderer-default");

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // Keep the accumulated target opaque: with backend defaults a translucent layer replaced
    // target alpha with its mask alpha, re-multiplying the composited scene at present.
    pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_alpha = sg_make_pipeline(&pip_desc);

    // Offscreen targets accumulate colour already multiplied by alpha; this turns them back into straight alpha.
    const std::string unpremul_fragment_source =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D tex;\n"
        "uniform vec4 tint;\n"
        "in vec2 uv;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  vec4 c = texture(tex, uv);\n"
        "  if (c.a > 0.0) c.rgb = min(c.rgb / c.a, vec3(1.0));\n"
        "  frag_color = c * tint;\n"
        "}\n";
    sg_pipeline_desc unpremul_desc = pip_desc;
    unpremul_desc.shader =
        create_backend_shader(&shd_desc, vertex_source, unpremul_fragment_source, "renderer-unpremul");
    unpremul_desc.colors[0].blend = {};
    r->pip_unpremul = sg_make_pipeline(&unpremul_desc);

    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    r->pip_add = sg_make_pipeline(&pip_desc);

    pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_lines = sg_make_pipeline(&pip_desc);

    const std::string mesh_vertex_source =
        "#version 330\n"
        "uniform mat4 mvp;\n"
        "layout(location=0) in vec3 position;\n"
        "layout(location=1) in vec2 texcoord0;\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "  gl_Position = mvp * vec4(position, 1.0);\n"
        "  uv = texcoord0;\n"
        "}\n";
    const std::string mesh_fragment_source =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D tex;\n"
        "uniform vec4 tint;\n"
        "in vec2 uv;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  frag_color = texture(tex, uv) * tint;\n"
        "}\n";

    sg_shader_desc mesh_shd_desc = {};
    mesh_shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    mesh_shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    mesh_shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    mesh_shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    mesh_shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    mesh_shd_desc.uniform_blocks[1].size = sizeof(float) * 4;
    mesh_shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "tint";
    mesh_shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    mesh_shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    mesh_shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    mesh_shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    mesh_shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    mesh_shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    mesh_shd_desc.texture_sampler_pairs[0].glsl_name = "tex";
    mesh_shd_desc.texture_sampler_pairs[0].view_slot = 0;
    mesh_shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    sg_shader mesh_shd =
        create_backend_shader(&mesh_shd_desc, mesh_vertex_source, mesh_fragment_source, "renderer-mesh");

    sg_pipeline_desc mesh_pip_desc = {};
    mesh_pip_desc.shader = mesh_shd;
    mesh_pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    mesh_pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    mesh_pip_desc.layout.attrs[1].buffer_index = 1;
    mesh_pip_desc.index_type = SG_INDEXTYPE_UINT16;
    mesh_pip_desc.colors[0].blend.enabled = true;
    mesh_pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    mesh_pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    mesh_pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    mesh_pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_mesh = sg_make_pipeline(&mesh_pip_desc);

    for (int mode = 0; mode <= kLastWallpaperBlendMode; ++mode) {
        r->pip_image_composite[mode] = {};
        r->shd_image_composite[mode] = {};
    }
}

// DO NOT EDIT: precompiles all blend pipelines during init to prevent GPU context loss mid-render (crash fix)
void renderer_precompile_blend_pipelines(EngineContext& ctx, renderer_t* r) {
    const int count = kLastWallpaperBlendMode - kFirstWallpaperBlendMode + 1;

    std::vector<std::future<BlendShaderSources>> futures;
    futures.reserve(count);
    for (int mode = kFirstWallpaperBlendMode; mode <= kLastWallpaperBlendMode; ++mode) {
        if (r->pip_image_composite[mode].id != SG_INVALID_ID) continue;
        futures.push_back(std::async(std::launch::async, prepareBlendShaderSources, std::ref(ctx), mode));
    }

    for (auto& f : futures) {
        BlendShaderSources sources = f.get();
        if (!sources.valid) continue;
        if (r->pip_image_composite[sources.mode].id != SG_INVALID_ID) continue;
        finalizeBlendPipeline(sources, r->shd_image_composite[sources.mode], r->pip_image_composite[sources.mode]);
    }
}

void renderer_cleanup(renderer_t* r) {
    r->pip_alpha = {};
    r->pip_add = {};
    r->pip_unpremul = {};
    r->pip_lines = {};
    for (auto& pipeline : r->pip_image_composite) pipeline = {};
    for (auto& shader : r->shd_image_composite) shader = {};
    r->vertex_buffer = {};
    r->fullscreen_vertex_buffer = {};
    r->index_buffer = {};
    r->smp_repeat = {};
    r->smp_clamp = {};
    r->white_view = {};
    r->white_pixel = {};
    r->black_view = {};
    r->black_pixel = {};
    r->gray_view = {};
    r->gray_pixel = {};
}

void renderer_update_viewport(renderer_t* renderer, float width, float height) {
    renderer->view_width = width;
    renderer->view_height = height;
}
