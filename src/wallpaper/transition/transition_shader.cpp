#include "wallpaper/transition/transition_shader.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "shared/graphics/render.h"
#include "shared/graphics/shader/shader_backend.h"

namespace {
bool readAssetText(EngineContext& ctx, const char* relative_path, std::string& out) {
    char path[1024] = {};
    if (!ctx.asset_mgr.resolvePath(relative_path, path, sizeof(path))) return false;
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

// Wallpaper Engine ships these only as DX11 fallback HLSL. Give Slang the same
// explicit Vulkan bindings the GLSL path would have generated. `fragment`
// gates the parts only the fragment shader declares.
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
    shader_ = create_backend_shader_hlsl(&desc, vertex_source, fragment_source, label);
    if (shader_.id == SG_INVALID_ID) {
        LOG_TAG_W("TRANSITION", "Transition shader '%s' failed to compile; using the built-in fade", label);
        shutdown();
        return false;
    }

    sg_pipeline_desc pipeline_desc = {};
    pipeline_desc.shader = shader_;
    pipeline_desc.layout.buffers[0].stride = sizeof(vertex_t);
    pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc.layout.attrs[0].offset = 0;
    pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc.layout.attrs[1].offset = sizeof(float) * 2;
    pipeline_desc.index_type = SG_INDEXTYPE_UINT16;
    // The shader premultiplies rgb by its mask alpha, so the usual (ONE,
    // ONE_MINUS_SRC_ALPHA) premultiplied blend reveals the new wallpaper.
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

    noise_image_ = ctx.asset_mgr.resolveTexture("util/noise");
    clouds_image_ = ctx.asset_mgr.resolveTexture("util/clouds_256");
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

    effect_index_ = effect_index;
    LOG_TAG_I("TRANSITION", "Loaded Wallpaper Engine transition shader '%s'", label);
    return true;
}

void TransitionShader::shutdown() {
    noise_view_ = GfxView();
    clouds_view_ = GfxView();
    noise_image_ = GfxImage();
    clouds_image_ = GfxImage();
    pipeline_ = GfxPipeline();
    shader_ = GfxShader();
    effect_index_ = -1;
}

void TransitionShader::drawOldOverNew(EngineContext& ctx, sg_view old_frame, float progress, int width, int height) {
    if (!ready() || old_frame.id == SG_INVALID_ID) return;

    sg_apply_pipeline(pipeline_);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = ctx.renderer.fullscreen_vertex_buffer;
    bind.index_buffer = ctx.renderer.index_buffer;
    bind.views[0] = old_frame;
    bind.views[1] = noise_view_.id != SG_INVALID_ID ? (sg_view)noise_view_ : (sg_view)ctx.renderer.white_view;
    bind.views[2] = clouds_view_.id != SG_INVALID_ID ? (sg_view)clouds_view_ : (sg_view)ctx.renderer.white_view;
    for (int slot = 3; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) bind.views[slot] = (sg_view){SG_INVALID_ID};
    for (int slot = 0; slot < SG_MAX_SAMPLER_BINDSLOTS; ++slot) bind.samplers[slot] = ctx.renderer.smp_repeat;
    bind.samplers[0] = ctx.renderer.smp_clamp;
    sg_apply_bindings(&bind);

    DynamicUniforms uniforms = {};
    uniforms.progress = progress;
    uniforms.hash = 0.5f;
    uniforms.hash2 = 0.25f;
    uniforms.random = 0.0f;
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
    sg_draw(0, 6, 1);
    ctx.renderer.draw_calls++;
}
