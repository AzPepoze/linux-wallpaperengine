#include <math.h>

#include <algorithm>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "render.h"
#include "render_internal.h"
#include "shader/shader_backend.h"
#include "shader/shader_compiler.h"
#include "shader/shader_processor.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/task_pool.h"
#include "shared/core/vfs.h"
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

// Thread-safe: reads asset files, processes sources and compiles SPIR-V — no GPU calls.
BlendShaderSources prepareBlendShaderSources(const IAssetResolver& assets, int blend_mode) {
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
        vertex_source, "shaders/linux-wallpaperengine/image_composite.vert", assets, true);
    std::string processed_frag = ShaderSourceProcessor::processShaderSource(
        fragment_source, "shaders/linux-wallpaperengine/image_composite.frag", assets, false);

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

    ShaderCompiler::prewarm("image-composite-" + std::to_string(blend_mode), result.vert, result.frag, {}, 1);
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

struct BlendPipelinePrecompileJob {
    std::string engine_path;
    std::string wallpaper_path;
    vfs::PackageHandle package;
    std::vector<std::future<BlendShaderSources>> futures;
};

namespace {
const std::string kSpriteVertexSource =
    "#version 330\n"
    "uniform mat4 mvp;\n"
    "layout(location=0) in vec2 position;\n"
    "layout(location=1) in vec2 texcoord0;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "  gl_Position = mvp * vec4(position, 0.0, 1.0);\n"
    "  uv = texcoord0;\n"
    "}\n";
const std::string kSpriteFragmentSource =
    "#version 330\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 tint;\n"
    "in vec2 uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = texture(tex, uv) * tint;\n"
    "}\n";

// Offscreen targets accumulate colour already multiplied by alpha; this turns them back into straight alpha.
// Fully transparent texels borrow the colour of nearby opaque ones, otherwise bilinear minification of the
// straight result mixes their black into every cut-out edge.
const std::string kUnpremulFragmentSource =
    "#version 330\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 tint;\n"
    "in vec2 uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  vec4 c = texture(tex, uv);\n"
    "  if (c.a > 0.0) {\n"
    "    c.rgb = min(c.rgb / c.a, vec3(1.0));\n"
    "  } else {\n"
    "    vec2 texel = vec2(abs(dFdx(uv.x)), abs(dFdy(uv.y)));\n"
    "    vec3 sum = vec3(0.0);\n"
    "    float weight = 0.0;\n"
    "    for (int y = -2; y <= 2; ++y) {\n"
    "      for (int x = -2; x <= 2; ++x) {\n"
    "        vec4 n = texture(tex, uv + vec2(float(x), float(y)) * texel);\n"
    "        sum += n.rgb;\n"
    "        weight += n.a;\n"
    "      }\n"
    "    }\n"
    "    if (weight > 0.0) c.rgb = min(sum / weight, vec3(1.0));\n"
    "  }\n"
    "  frag_color = c * tint;\n"
    "}\n";

const std::string kPresentFragmentSource =
    "#version 330\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 tint;\n"
    "in vec2 uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  vec4 c = texture(tex, uv) * tint;\n"
    "  vec3 x = max(c.rgb, vec3(0.0));\n"
    "  const float knee = 0.75;\n"
    "  const float range = 1.0 - knee;\n"
    "  vec3 rolled = knee + range * (vec3(1.0) - exp(-max(x - knee, vec3(0.0)) / range));\n"
    "  x = mix(x, rolled, step(vec3(knee), x));\n"
    "  frag_color = vec4(x, c.a);\n"
    "}\n";

const std::string kMeshVertexSource =
    "#version 330\n"
    "uniform mat4 mvp;\n"
    "layout(location=0) in vec3 position;\n"
    "layout(location=1) in vec2 texcoord0;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "  gl_Position = mvp * vec4(position, 1.0);\n"
    "  uv = texcoord0;\n"
    "}\n";
const std::string kMeshFragmentSource =
    "#version 330\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 tint;\n"
    "in vec2 uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = texture(tex, uv) * tint;\n"
    "}\n";

sg_shader_desc spriteShaderDesc() {
    sg_shader_desc desc = {};
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = sizeof(mat4x4);
    desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[1].size = sizeof(float) * 4;
    desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "tint";
    desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[0].glsl_name = "tex";
    desc.texture_sampler_pairs[0].view_slot = 0;
    desc.texture_sampler_pairs[0].sampler_slot = 0;
    return desc;
}

sg_shader makeSpriteShader(const std::string& vertex_source, const std::string& fragment_source, const char* label) {
    sg_shader_desc desc = spriteShaderDesc();
    return create_backend_shader(&desc, vertex_source, fragment_source, label);
}

// Straight-alpha blending that keeps the accumulated target opaque: with backend defaults a translucent layer
// replaced target alpha with its mask alpha, re-multiplying the composited scene at present.
void useAlphaBlend(sg_pipeline_desc& desc) {
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
}

void createGeometryBuffers(renderer_t* r) {
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
}

void createSamplers(renderer_t* r) {
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
}

void createFallbackTextures(renderer_t* r) {
    const auto makeSolid = [](uint32_t pixel, GfxImage& image, GfxView& view) {
        sg_image_desc img_desc = {};
        img_desc.width = 1;
        img_desc.height = 1;
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        img_desc.data.mip_levels[0] = {&pixel, 4};
        image = sg_make_image(&img_desc);
        sg_view_desc view_desc = {};
        view_desc.texture.image = image;
        view = sg_make_view(&view_desc);
    };
    makeSolid(0xFFFFFFFF, r->white_pixel, r->white_view);
    makeSolid(0x00000000, r->black_pixel, r->black_view);
    makeSolid(0x808080FF, r->gray_pixel, r->gray_view);
}

void createSpritePipelines(renderer_t* r) {
    const sg_shader sprite_shader = makeSpriteShader(kSpriteVertexSource, kSpriteFragmentSource, "renderer-default");

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = sprite_shader;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    useAlphaBlend(pip_desc);
    r->pip_alpha = sg_make_pipeline(&pip_desc);
    pip_desc.colors[0].blend.enabled = false;
    r->pip_copy = sg_make_pipeline(&pip_desc);
    pip_desc.colors[0].blend.enabled = true;

    sg_pipeline_desc unpremul_desc = pip_desc;
    unpremul_desc.shader = makeSpriteShader(kSpriteVertexSource, kUnpremulFragmentSource, "renderer-unpremul");
    unpremul_desc.colors[0].blend = {};
    r->pip_unpremul = sg_make_pipeline(&unpremul_desc);

    sg_pipeline_desc present_desc = pip_desc;
    present_desc.shader = makeSpriteShader(kSpriteVertexSource, kPresentFragmentSource, "renderer-present");
    present_desc.colors[0].blend = {};
    r->pip_present = sg_make_pipeline(&present_desc);

    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    r->pip_add = sg_make_pipeline(&pip_desc);

    pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_lines = sg_make_pipeline(&pip_desc);
}

void createMeshPipeline(renderer_t* r) {
    sg_pipeline_desc desc = {};
    desc.shader = makeSpriteShader(kMeshVertexSource, kMeshFragmentSource, "renderer-mesh");
    desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[1].buffer_index = 1;
    desc.index_type = SG_INDEXTYPE_UINT16;
    useAlphaBlend(desc);
    r->pip_mesh = sg_make_pipeline(&desc);
}
}  // namespace

void renderer_init(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;

    createGeometryBuffers(r);
    createSamplers(r);
    createFallbackTextures(r);
    createSpritePipelines(r);
    createMeshPipeline(r);

    for (int mode = 0; mode <= kLastWallpaperBlendMode; ++mode) {
        r->pip_image_composite[mode] = {};
        r->shd_image_composite[mode] = {};
    }
}

BlendPipelinePrecompileJobHandle renderer_begin_blend_pipeline_precompile(const AssetManager& assets,
                                                                          vfs::PackageHandle package, renderer_t* r,
                                                                          const std::vector<int>& modes) {
    const int count = kLastWallpaperBlendMode - kFirstWallpaperBlendMode + 1;
    auto job = std::make_shared<BlendPipelinePrecompileJob>();
    job->engine_path = assets.getEnginePath();
    job->wallpaper_path = assets.getWallpaperPath();
    job->package = std::move(package);
    job->futures.reserve(count);
    for (int mode = kFirstWallpaperBlendMode; mode <= kLastWallpaperBlendMode; ++mode) {
        if (!modes.empty() && std::find(modes.begin(), modes.end(), mode) == modes.end()) continue;
        if (r->pip_image_composite[mode].id != SG_INVALID_ID) continue;
        const std::string engine_path = job->engine_path;
        const std::string wallpaper_path = job->wallpaper_path;
        const vfs::PackageHandle worker_package = job->package;
        job->futures.push_back(TaskPool::instance().enqueue([engine_path, wallpaper_path, worker_package, mode] {
            vfs::ScopedBinding binding(worker_package);
            AssetManager worker_assets;
            worker_assets.init(engine_path.c_str(), wallpaper_path.c_str());
            return prepareBlendShaderSources(worker_assets, mode);
        }));
    }
    return job;
}

bool renderer_poll_blend_pipeline_precompile(const BlendPipelinePrecompileJobHandle& job, renderer_t* r,
                                             std::chrono::steady_clock::time_point deadline) {
    if (!job || job->futures.empty()) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;

    for (auto it = job->futures.begin(); it != job->futures.end(); ++it) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        if (it->wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        BlendShaderSources sources;
        bool prepared = false;
        try {
            sources = it->get();
            prepared = true;
        } catch (const std::exception& e) {
            LOG_TAG_E("RENDER", "Blend shader preparation failed: %s", e.what());
        } catch (...) {
            LOG_TAG_E("RENDER", "Blend shader preparation failed with an unknown exception");
        }
        job->futures.erase(it);
        if (prepared && sources.valid && r->pip_image_composite[sources.mode].id == SG_INVALID_ID)
            finalizeBlendPipeline(sources, r->shd_image_composite[sources.mode], r->pip_image_composite[sources.mode]);
        return job->futures.empty();
    }
    return false;
}

void renderer_precompile_blend_pipelines(EngineContext& ctx, renderer_t* r, const std::vector<int>& modes) {
    if (!ctx.asset_mgr) return;
    auto job = renderer_begin_blend_pipeline_precompile(*ctx.asset_mgr, vfs::currentPackage(), r, modes);
    while (!renderer_poll_blend_pipeline_precompile(job, r, std::chrono::steady_clock::time_point::max())) {
        if (!job->futures.empty())
            job->futures.front().wait();
        else
            std::this_thread::yield();
    }
}

void renderer_cleanup(renderer_t* r) {
    r->pip_alpha = {};
    r->pip_copy = {};
    r->pip_add = {};
    r->pip_unpremul = {};
    r->pip_present = {};
    r->pip_lines = {};
    r->pip_mesh = {};
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
