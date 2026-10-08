#include "wallpaper/transition/transition_shader.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "shared/graphics/render.h"
#include "shared/graphics/shader/shader_backend.h"

namespace {
bool readAssetText(EngineContext& ctx, const char* relative_path, std::string& out) {
    char path[1024] = {};
    if (!ctx.asset_mgr->resolvePath(relative_path, path, sizeof(path))) return false;
    FILE* file = vfs::open(path);
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return false;
    }
    out.resize((size_t)size);
    const size_t read = fread(out.data(), 1, (size_t)size, file);
    fclose(file);
    out.resize(read);
    return read > 0;
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// --- Bricks (#16) geometry, ported from dx11playlisttransition.geom ---------
struct BrickVec2 {
    float x;
    float y;
};

constexpr int kBrickSetCount = 4;
constexpr int kBricksPerSet = 7;
constexpr int kBrickCount = kBrickSetCount * kBricksPerSet;
constexpr int kBrickVertexCount = kBrickCount * 6;  // two triangles per brick

float smoothstepF(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

BrickVec2 rotateF(BrickVec2 v, float r) {
    const float cs = cosf(r);
    const float sn = sinf(r);
    return {v.x * cs - v.y * sn, v.x * sn + v.y * cs};
}

void makeBrickGeom(BrickVec2 origin, float angle, BrickVec2 size, BrickVec2 uv_origin, float aspect,
                   std::vector<vertex_t>& out) {
    BrickVec2 half = {size.x * 0.5f * aspect, size.y * 0.5f};
    BrickVec2 hx = rotateF({half.x, 0.0f}, angle);
    BrickVec2 hy = rotateF({0.0f, half.y}, angle);
    hx.x /= aspect;
    hx.y /= aspect;
    hy.x /= aspect;
    hy.y /= aspect;

    const BrickVec2 a00 = {origin.x - hx.x - hy.x, origin.y - hx.y - hy.y};
    const BrickVec2 a01 = {origin.x - hx.x + hy.x, origin.y - hx.y + hy.y};
    const BrickVec2 a10 = {origin.x + hx.x - hy.x, origin.y + hx.y - hy.y};
    const BrickVec2 a11 = {origin.x + hx.x + hy.x, origin.y + hx.y + hy.y};

    const BrickVec2 hs = {size.x * 0.5f, size.y * 0.5f};
    const auto uv = [](BrickVec2 p) {
        BrickVec2 u = {p.x * 0.5f + 0.5f, p.y * 0.5f + 0.5f};
        u.y = 1.0f - u.y;
        return u;
    };
    const BrickVec2 u00 = uv({uv_origin.x - hs.x, uv_origin.y - hs.y});
    const BrickVec2 u01 = uv({uv_origin.x - hs.x, uv_origin.y + hs.y});
    const BrickVec2 u10 = uv({uv_origin.x + hs.x, uv_origin.y - hs.y});
    const BrickVec2 u11 = uv({uv_origin.x + hs.x, uv_origin.y + hs.y});

    // Triangle strip (00,01,10,11) expanded to two triangles.
    out.push_back({a00.x, a00.y, u00.x, u00.y});
    out.push_back({a01.x, a01.y, u01.x, u01.y});
    out.push_back({a10.x, a10.y, u10.x, u10.y});
    out.push_back({a01.x, a01.y, u01.x, u01.y});
    out.push_back({a10.x, a10.y, u10.x, u10.y});
    out.push_back({a11.x, a11.y, u11.x, u11.y});
}

void makeBrick(BrickVec2 origin, BrickVec2 size, float progress, float aspect, std::vector<vertex_t>& out) {
    const float anim_pos_y = origin.y * 0.5f + 0.5f;
    const float anim_pos_x = origin.x * 0.5f + 0.5f;
    const float fall_duration = 0.3f;
    float fall_offset =
        smoothstepF(0.0f, fall_duration, progress * (1.0f + fall_duration * 1.5f) - anim_pos_y - anim_pos_x * 0.2f);
    fall_offset *= fall_offset;

    BrickVec2 anim_origin = origin;
    anim_origin.y -= fall_offset * (anim_pos_y + 0.2f) * 2.6f;
    anim_origin.x += fall_offset * origin.x * 0.333f;
    const float angle = fall_offset * 3.0f * (-origin.x);
    makeBrickGeom(anim_origin, angle, size, origin, aspect, out);
}

void generateBricks(float progress, float aspect, std::vector<vertex_t>& out) {
    out.clear();
    out.reserve(kBrickVertexCount);
    const float set_height = 2.0f / (float)kBrickSetCount;
    const float brick_height = set_height * 0.5f;
    const float brick_width = 2.0f * 0.333334f;
    const float brick_width_half = brick_width * 0.5f;
    const float brick_height_half = brick_height * 0.5f;

    BrickVec2 pos = {-1.0f, -1.0f};
    for (int set = 0; set < kBrickSetCount; ++set) {
        pos.x = -1.0f;
        for (int i = 0; i < 3; ++i) {
            makeBrick({pos.x + brick_width_half, pos.y + brick_height_half}, {brick_width, brick_height}, progress,
                      aspect, out);
            pos.x += brick_width;
        }
        pos.x = -1.0f - brick_width_half;
        pos.y += brick_height;
        for (int i = 0; i < 4; ++i) {
            makeBrick({pos.x + brick_width_half, pos.y + brick_height_half}, {brick_width, brick_height}, progress,
                      aspect, out);
            pos.x += brick_width;
        }
        pos.y += brick_height;
    }
}

// --- Glass shatter (#23): a tessellated shard plane ------------------------
struct ShatterVertex {
    float px;
    float py;
    float pz;
    float u;
    float v;
    float cx;
    float cy;
    float cz;
    float nx;
    float ny;
    float nz;
};

constexpr int kShatterGrid = 24;

void generateShatterMesh(std::vector<ShatterVertex>& out) {
    out.clear();
    out.reserve(kShatterGrid * kShatterGrid * 6);
    const float step = 2.0f / (float)kShatterGrid;
    const auto uvx = [](float x) { return x * 0.5f + 0.5f; };
    const auto uvy = [](float y) { return 1.0f - (y * 0.5f + 0.5f); };
    for (int j = 0; j < kShatterGrid; ++j) {
        for (int i = 0; i < kShatterGrid; ++i) {
            const float x0 = -1.0f + (float)i * step;
            const float x1 = x0 + step;
            const float y0 = -1.0f + (float)j * step;
            const float y1 = y0 + step;
            const float cx = (x0 + x1) * 0.5f;
            const float cy = (y0 + y1) * 0.5f;
            const ShatterVertex a = {x0, y0, 0.0f, uvx(x0), uvy(y0), cx, cy, 0.0f, 0.0f, 0.0f, 1.0f};
            const ShatterVertex b = {x1, y0, 0.0f, uvx(x1), uvy(y0), cx, cy, 0.0f, 0.0f, 0.0f, 1.0f};
            const ShatterVertex c = {x1, y1, 0.0f, uvx(x1), uvy(y1), cx, cy, 0.0f, 0.0f, 0.0f, 1.0f};
            const ShatterVertex d = {x0, y1, 0.0f, uvx(x0), uvy(y1), cx, cy, 0.0f, 0.0f, 0.0f, 1.0f};
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
            out.push_back(a);
            out.push_back(c);
            out.push_back(d);
        }
    }
}

// DX11 fallback HLSL gets the explicit Vulkan bindings the GLSL path would generate; `fragment` selects fragment-only parts.
std::string annotateHlsl(std::string source, int effect_index, bool fragment) {
    if (fragment) {
        replaceAll(source, "Texture2D g_Texture0MipMapped:register(t0);", "");
    }

    std::string prefix = "#define FADEEFFECT " + std::to_string(effect_index) + "\n";
    if (fragment) prefix += "#define g_Texture0MipMapped g_Texture0\n";
    source = prefix + source;

    if (fragment) {
        replaceAll(source, "Texture2D g_Texture0:register(t0);",
                   "[[vk::binding(0,1)]] Texture2D g_Texture0:register(t0);");
        replaceAll(source, "Texture2D g_Texture1Noise:register(t1);",
                   "[[vk::binding(1,1)]] Texture2D g_Texture1Noise:register(t1);");
        replaceAll(source, "Texture2D g_Texture2Clouds:register(t2);",
                   "[[vk::binding(2,1)]] Texture2D g_Texture2Clouds:register(t2);");
        replaceAll(source, "SamplerState g_Texture0SamplerState:register(s0);",
                   "[[vk::binding(32,1)]] SamplerState g_Texture0SamplerState:register(s0);");
        replaceAll(source, "SamplerState g_Texture0SamplerStateWrap:register(s1);",
                   "[[vk::binding(33,1)]] SamplerState g_Texture0SamplerStateWrap:register(s1);");
        replaceAll(source, "cbuffer g_bufDynamic:register(b0)", "[[vk::binding(0,0)]] cbuffer g_bufDynamic");
    } else {
        // The vertex block needs its own set-0 binding to avoid colliding with the fragment block.
        replaceAll(source, "cbuffer g_bufDynamic:register(b0)", "[[vk::binding(1,0)]] cbuffer g_bufDynamic");
        if (effect_index == (int)lwe::transition::Effect::GlassShatter) {
            // fxc truncates float3 -> float2 implicitly; Slang rejects it.
            replaceAll(source, "nrand(IN.a_Center *", "nrand(IN.a_Center.xy *");
        }
    }
    return source;
}
}  // namespace

bool TransitionShader::init(EngineContext& ctx, int effect_index) {
    if (effect_index < 0 || effect_index >= lwe::transition::effectCount()) return false;
    if (effect_index_ == effect_index && ready()) return true;
    shutdown();

    std::string vertex_source;
    std::string fragment_source;
    if (!readAssetText(ctx, "shaders/HLSL/dx11playlisttransition.vert", vertex_source) ||
        !readAssetText(ctx, "shaders/HLSL/dx11playlisttransition.frag", fragment_source)) {
        LOG_TAG_W("TRANSITION", "The install has no dx11playlisttransition shader; using the built-in fade");
        return false;
    }
    vertex_source = annotateHlsl(vertex_source, effect_index, /*fragment=*/false);
    fragment_source = annotateHlsl(fragment_source, effect_index, /*fragment=*/true);

    sg_shader_desc desc = {};
    desc.attrs[0].glsl_name = "a_Position";
    desc.attrs[1].glsl_name = "a_TexCoord";
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[0].size = sizeof(DynamicUniforms);
    desc.uniform_blocks[0].spirv_set0_binding_n = 0;

    for (int slot = 0; slot < 3; ++slot) {
        desc.views[slot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        desc.views[slot].texture.image_type = SG_IMAGETYPE_2D;
        desc.views[slot].texture.spirv_set1_binding_n = (uint8_t)slot;
    }
    for (int slot = 0; slot < 2; ++slot) {
        desc.samplers[slot].stage = SG_SHADERSTAGE_FRAGMENT;
        desc.samplers[slot].sampler_type = SG_SAMPLERTYPE_FILTERING;
        desc.samplers[slot].spirv_set1_binding_n = (uint8_t)(SG_MAX_VIEW_BINDSLOTS + slot);
    }
    desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[0].view_slot = 0;
    desc.texture_sampler_pairs[0].sampler_slot = 0;
    desc.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[1].view_slot = 1;
    desc.texture_sampler_pairs[1].sampler_slot = 1;
    desc.texture_sampler_pairs[2].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.texture_sampler_pairs[2].view_slot = 2;
    desc.texture_sampler_pairs[2].sampler_slot = 1;

    const char* label = lwe::transition::effectByIndex(effect_index)->name;
    const bool is_shatter = effect_index == (int)lwe::transition::Effect::GlassShatter;
    if (is_shatter) {
        // The vertex stage also reads the dynamic block.
        desc.uniform_blocks[1].stage = SG_SHADERSTAGE_VERTEX;
        desc.uniform_blocks[1].size = sizeof(DynamicUniforms);
        desc.uniform_blocks[1].spirv_set0_binding_n = 1;
    }
    shader_ = create_backend_shader_hlsl(&desc, vertex_source, fragment_source, label);
    if (shader_.id == SG_INVALID_ID) {
        LOG_TAG_W("TRANSITION", "Transition shader '%s' failed to compile; using the built-in fade", label);
        shutdown();
        return false;
    }

    sg_pipeline_desc pipeline_desc = {};
    pipeline_desc.shader = shader_;
    if (is_shatter) {
        pipeline_desc.layout.buffers[0].stride = sizeof(ShatterVertex);
        pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pipeline_desc.layout.attrs[0].offset = 0;
        pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pipeline_desc.layout.attrs[1].offset = sizeof(float) * 3;
        pipeline_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
        pipeline_desc.layout.attrs[2].offset = sizeof(float) * 5;
        pipeline_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
        pipeline_desc.layout.attrs[3].offset = sizeof(float) * 8;
    } else {
        pipeline_desc.layout.buffers[0].stride = sizeof(vertex_t);
        pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pipeline_desc.layout.attrs[0].offset = 0;
        pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pipeline_desc.layout.attrs[1].offset = sizeof(float) * 2;
    }
    pipeline_desc.index_type = SG_INDEXTYPE_UINT16;
    // rgb is premultiplied by the mask alpha, so the premultiplied blend reveals the new wallpaper.
    pipeline_desc.colors[0].blend.enabled = true;
    pipeline_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    pipeline_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline_ = sg_make_pipeline(&pipeline_desc);
    if (pipeline_.id == SG_INVALID_ID) {
        shutdown();
        return false;
    }

    noise_image_ = ctx.asset_mgr->resolveTexture("util/noise");
    clouds_image_ = ctx.asset_mgr->resolveTexture("util/clouds_256");
    if (noise_image_.id != SG_INVALID_ID) {
        sg_view_desc view_desc = {};
        view_desc.texture.image = noise_image_;
        noise_view_ = sg_make_view(&view_desc);
    }
    if (clouds_image_.id != SG_INVALID_ID) {
        sg_view_desc view_desc = {};
        view_desc.texture.image = clouds_image_;
        clouds_view_ = sg_make_view(&view_desc);
    }

    if (effect_index == (int)lwe::transition::Effect::Bricks) {
        sg_buffer_desc vertex_desc = {};
        vertex_desc.usage.stream_update = true;
        vertex_desc.size = kBrickVertexCount * sizeof(vertex_t);
        brick_vertices_ = sg_make_buffer(&vertex_desc);

        uint16_t indices[kBrickVertexCount];
        for (int i = 0; i < kBrickVertexCount; ++i) indices[i] = (uint16_t)i;
        sg_buffer_desc index_desc = {};
        index_desc.usage.index_buffer = true;
        index_desc.data = {indices, sizeof(indices)};
        brick_indices_ = sg_make_buffer(&index_desc);
    }

    if (is_shatter) {
        std::vector<ShatterVertex> mesh;
        generateShatterMesh(mesh);
        sg_buffer_desc vertex_desc = {};
        vertex_desc.data = {mesh.data(), mesh.size() * sizeof(ShatterVertex)};
        shatter_vertices_ = sg_make_buffer(&vertex_desc);

        std::vector<uint16_t> indices(mesh.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = (uint16_t)i;
        sg_buffer_desc index_desc = {};
        index_desc.usage.index_buffer = true;
        index_desc.data = {indices.data(), indices.size() * sizeof(uint16_t)};
        shatter_indices_ = sg_make_buffer(&index_desc);
        shatter_index_count_ = (int)mesh.size();
    }

    effect_index_ = effect_index;
    LOG_TAG_I("TRANSITION", "Loaded Wallpaper Engine transition shader '%s'", label);
    return true;
}

void TransitionShader::shutdown() {
    noise_view_ = GfxView();
    clouds_view_ = GfxView();
    noise_image_ = GfxImage();
    clouds_image_ = GfxImage();
    brick_vertices_ = GfxBuffer();
    brick_indices_ = GfxBuffer();
    shatter_vertices_ = GfxBuffer();
    shatter_indices_ = GfxBuffer();
    shatter_index_count_ = 0;
    pipeline_ = GfxPipeline();
    shader_ = GfxShader();
    effect_index_ = -1;
}

void TransitionShader::drawOldOverNew(EngineContext& ctx, sg_view old_frame, float progress, int width, int height,
                                      uint32_t hash_seed) {
    if (!ready() || old_frame.id == SG_INVALID_ID) return;

    sg_apply_pipeline(pipeline_);
    sg_bindings bind = {};
    int draw_count = 6;
    if (effect_index_ == (int)lwe::transition::Effect::GlassShatter && shatter_vertices_.id != SG_INVALID_ID) {
        bind.vertex_buffers[0] = shatter_vertices_;
        bind.index_buffer = shatter_indices_;
        draw_count = shatter_index_count_;
    } else if (effect_index_ == (int)lwe::transition::Effect::Bricks && brick_vertices_.id != SG_INVALID_ID) {
        std::vector<vertex_t> vertices;
        generateBricks(progress, height > 0 ? (float)width / (float)height : 1.0f, vertices);
        sg_update_buffer(brick_vertices_, {vertices.data(), vertices.size() * sizeof(vertex_t)});
        bind.vertex_buffers[0] = brick_vertices_;
        bind.index_buffer = brick_indices_;
        draw_count = kBrickVertexCount;
    } else {
        bind.vertex_buffers[0] = ctx.renderer.fullscreen_vertex_buffer;
        bind.index_buffer = ctx.renderer.index_buffer;
    }
    bind.views[0] = old_frame;
    bind.views[1] = noise_view_.id != SG_INVALID_ID ? (sg_view)noise_view_ : (sg_view)ctx.renderer.white_view;
    bind.views[2] = clouds_view_.id != SG_INVALID_ID ? (sg_view)clouds_view_ : (sg_view)ctx.renderer.white_view;
    for (int slot = 3; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) bind.views[slot] = (sg_view){SG_INVALID_ID};
    for (int slot = 0; slot < SG_MAX_SAMPLER_BINDSLOTS; ++slot) bind.samplers[slot] = ctx.renderer.smp_repeat;
    bind.samplers[0] = ctx.renderer.smp_clamp;
    sg_apply_bindings(&bind);

    DynamicUniforms uniforms = {};
    uniforms.progress = progress;
    // Per-switch randomness: the WE shaders read g_Hash/g_Hash2, so each switch differs.
    uniforms.hash = (float)(hash_seed & 0xffffu) / 65535.0f;
    uniforms.hash2 = (float)((hash_seed >> 16) & 0xffffu) / 65535.0f;
    uniforms.random = (float)(hash_seed % 97u) / 97.0f;
    uniforms.aspect = height > 0 ? (float)width / (float)height : 1.0f;
    uniforms.width = (float)width;
    uniforms.height = (float)height;
    uniforms.view_projection[0] = 1.0f;
    uniforms.view_projection[5] = 1.0f;
    uniforms.view_projection[10] = 1.0f;
    uniforms.view_projection[15] = 1.0f;
    uniforms.view_projection_inv[0] = 1.0f;
    uniforms.view_projection_inv[5] = 1.0f;
    uniforms.view_projection_inv[10] = 1.0f;
    uniforms.view_projection_inv[15] = 1.0f;

    sg_range range = {.ptr = &uniforms, .size = sizeof(uniforms)};
    sg_apply_uniforms(0, &range);
    if (effect_index_ == (int)lwe::transition::Effect::GlassShatter) sg_apply_uniforms(1, &range);
    sg_draw(0, draw_count, 1);
    ctx.renderer.draw_calls++;
}
