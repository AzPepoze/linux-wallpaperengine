#include "shader_compiler.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/logger.h"
#include "render/render.h"
#include "shader_backend.h"
#include "shader_uniform_layout.h"

namespace {

bool usesParticleSpriteLayout(const std::string& source) {
    return source.find("a_TexCoordVec4") != std::string::npos && source.find("a_TexCoordVec4C1") != std::string::npos &&
           source.find("a_TexCoordC2") != std::string::npos && source.find("a_Color") != std::string::npos;
}

void configureParticleBuiltins(sg_shader_desc& desc) {
    constexpr int slot = 3;
    desc.uniform_blocks[slot].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[slot].size = sizeof(particle_builtin_uniforms_t);

    const char* names[] = {"g_ModelMatrix",   "g_ModelMatrixInverse", "g_ViewProjectionMatrix",
                           "g_OrientationUp", "g_OrientationRight",   "g_OrientationForward",
                           "g_ViewUp",        "g_ViewRight",          "g_EyePosition",
                           "g_RenderVar0",    "g_RenderVar1"};
    const sg_uniform_type types[] = {SG_UNIFORMTYPE_MAT4,   SG_UNIFORMTYPE_MAT4,   SG_UNIFORMTYPE_MAT4,
                                     SG_UNIFORMTYPE_FLOAT3, SG_UNIFORMTYPE_FLOAT3, SG_UNIFORMTYPE_FLOAT3,
                                     SG_UNIFORMTYPE_FLOAT3, SG_UNIFORMTYPE_FLOAT3, SG_UNIFORMTYPE_FLOAT3,
                                     SG_UNIFORMTYPE_FLOAT4, SG_UNIFORMTYPE_FLOAT4};
    constexpr int count = sizeof(names) / sizeof(names[0]);
    for (int index = 0; index < count; ++index) {
        desc.uniform_blocks[slot].glsl_uniforms[index].glsl_name = names[index];
        desc.uniform_blocks[slot].glsl_uniforms[index].type = types[index];
    }
}

}  // namespace

GfxPipeline ShaderCompiler::makePipeline(sg_shader shader, ShaderVertexLayout layout, ShaderBlendMode blend_mode) {
    if (shader.id == SG_INVALID_ID) return {};

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shader;
    pip_desc.cull_mode = SG_CULLMODE_NONE;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.stencil.enabled = false;

    if (layout == ShaderVertexLayout::ParticleSprite) {
        pip_desc.layout.buffers[0].stride = sizeof(float) * 17;
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip_desc.layout.attrs[0].offset = 0;
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip_desc.layout.attrs[1].offset = sizeof(float) * 3;
        pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
        pip_desc.layout.attrs[2].offset = sizeof(float) * 7;
        pip_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4;
        pip_desc.layout.attrs[3].offset = sizeof(float) * 11;
        pip_desc.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT2;
        pip_desc.layout.attrs[4].offset = sizeof(float) * 15;
        pip_desc.index_type = SG_INDEXTYPE_UINT32;
    } else {
        pip_desc.layout.buffers[0].stride = sizeof(vertex_t);
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip_desc.layout.attrs[0].offset = 0;
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pip_desc.layout.attrs[1].offset = sizeof(float) * 2;
        pip_desc.index_type = SG_INDEXTYPE_UINT16;
    }

    if (blend_mode != ShaderBlendMode::Disabled) {
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb =
            blend_mode == ShaderBlendMode::Additive ? SG_BLENDFACTOR_ONE : SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }

    return sg_make_pipeline(&pip_desc);
}

CompiledShader ShaderCompiler::compile(const std::string& shader_name, const std::string& vertSource,
                                       const std::string& fragSource,
                                       const std::map<std::string, std::vector<float>>& uniforms, int textureCount) {
    CompiledShader result;
    std::string compiled_vert_source = vertSource;
    std::string compiled_frag_source = fragSource;
    result.vertex_layout = usesParticleSpriteLayout(compiled_vert_source) ? ShaderVertexLayout::ParticleSprite
                                                                          : ShaderVertexLayout::Sprite2D;

    sg_shader_desc shd_desc = {};
    if (result.vertex_layout == ShaderVertexLayout::ParticleSprite) {
        shd_desc.attrs[0].glsl_name = "a_Position";
        shd_desc.attrs[1].glsl_name = "a_TexCoordVec4";
        shd_desc.attrs[2].glsl_name = "a_Color";
        shd_desc.attrs[3].glsl_name = "a_TexCoordVec4C1";
        shd_desc.attrs[4].glsl_name = "a_TexCoordC2";
    } else {
        shd_desc.attrs[0].glsl_name = "a_Position";
        shd_desc.attrs[1].glsl_name = "a_TexCoord";
    }

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrix";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[1].size = sizeof(builtin_uniforms_t) - sizeof(mat4x4);
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrixInverse";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    const char* builtin_names[] = {"g_Texture0Resolution",
                                   "g_Texture1Resolution",
                                   "g_Texture2Resolution",
                                   "g_Texture3Resolution",
                                   "g_Texture4Resolution",
                                   "g_ParallaxPosition",
                                   "g_Time",
                                   "g_Padding1",
                                   "g_Screen",
                                   "g_TexelSize",
                                   "g_EffectTextureProjectionMatrix",
                                   "g_EffectTextureProjectionMatrixInverse",
                                   "g_PointerPosition",
                                   "g_Padding3"};
    const sg_uniform_type builtin_types[] = {SG_UNIFORMTYPE_FLOAT4, SG_UNIFORMTYPE_FLOAT4, SG_UNIFORMTYPE_FLOAT4,
                                             SG_UNIFORMTYPE_FLOAT4, SG_UNIFORMTYPE_FLOAT4, SG_UNIFORMTYPE_FLOAT2,
                                             SG_UNIFORMTYPE_FLOAT,  SG_UNIFORMTYPE_FLOAT,  SG_UNIFORMTYPE_FLOAT2,
                                             SG_UNIFORMTYPE_FLOAT2, SG_UNIFORMTYPE_MAT4,   SG_UNIFORMTYPE_MAT4,
                                             SG_UNIFORMTYPE_FLOAT2, SG_UNIFORMTYPE_FLOAT2};
    constexpr int kBuiltinMemberCount = sizeof(builtin_names) / sizeof(builtin_names[0]);
    for (int i = 0; i < kBuiltinMemberCount; ++i) {
        shd_desc.uniform_blocks[1].glsl_uniforms[i + 1].glsl_name = builtin_names[i];
        shd_desc.uniform_blocks[1].glsl_uniforms[i + 1].type = builtin_types[i];
    }

    shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[2].size = sizeof(builtin_uniforms_t) - sizeof(mat4x4) + sizeof(float) * 4;
    shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrixInverse";
    shd_desc.uniform_blocks[2].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    for (int i = 0; i < kBuiltinMemberCount; ++i) {
        shd_desc.uniform_blocks[2].glsl_uniforms[i + 1].glsl_name = builtin_names[i];
        shd_desc.uniform_blocks[2].glsl_uniforms[i + 1].type = builtin_types[i];
    }
    shd_desc.uniform_blocks[2].glsl_uniforms[kBuiltinMemberCount + 1].glsl_name = "tint";
    shd_desc.uniform_blocks[2].glsl_uniforms[kBuiltinMemberCount + 1].type = SG_UNIFORMTYPE_FLOAT4;

    int next_uniform_slot = 3;
    if (result.vertex_layout == ShaderVertexLayout::ParticleSprite) {
        configureParticleBuiltins(shd_desc);
        next_uniform_slot = 4;
    }

    std::string custom_uniform_names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS];
    configureCustomUniformBlocks(uniforms, compiled_vert_source, compiled_frag_source, shd_desc, result,
                                 next_uniform_slot, custom_uniform_names);

    static const char* kTextureNames[] = {"g_Texture0", "g_Texture1", "g_Texture2",  "g_Texture3",
                                          "g_Texture4", "g_Texture5", "g_Texture6",  "g_Texture7",
                                          "g_Texture8", "g_Texture9", "g_Texture10", "g_Texture11"};

    const int texture_slots = std::min(textureCount + 1, 12);
    for (int slot = 0; slot < texture_slots; ++slot) {
        shd_desc.views[slot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[slot].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.samplers[slot].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.samplers[slot].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd_desc.texture_sampler_pairs[slot].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[slot].glsl_name = kTextureNames[slot];
        shd_desc.texture_sampler_pairs[slot].view_slot = slot;
        shd_desc.texture_sampler_pairs[slot].sampler_slot = slot;
    }

    result.shader = create_backend_shader(&shd_desc, compiled_vert_source, compiled_frag_source, shader_name.c_str());

    if (result.shader.id == SG_INVALID_ID) {
        effect_log.error("Failed to create shader for %s", shader_name.c_str());
        return result;
    }

    result.pipeline = makePipeline(result.shader, result.vertex_layout, ShaderBlendMode::Disabled);

    if (result.pipeline.id == SG_INVALID_ID) {
        effect_log.error("Failed to create pipeline for %s", shader_name.c_str());
    } else {
        effect_log.info("Created pipeline for %s", shader_name.c_str());
    }

    return result;
}

#if DEBUG_BUILD
std::string ShaderCompiler::applyDebugMode(const std::string& fsSource, int debug_view_mode) {
    if (debug_view_mode == 0) return fsSource;

    std::string full_fs = fsSource;
    size_t pos = full_fs.rfind("frag_color = ");
    if (pos != std::string::npos) {
        size_t end = full_fs.find(';', pos);
        if (end != std::string::npos) {
            std::string debug_line;
            if (debug_view_mode >= 1 && debug_view_mode <= 10) {
                char buf[64];
                snprintf(buf, sizeof(buf), "frag_color = texSample2D(g_Texture%d, v_TexCoord.xy)", debug_view_mode - 1);
                debug_line = buf;
            } else if (debug_view_mode >= 11 && debug_view_mode <= 20) {
                char buf[128];
                snprintf(buf, sizeof(buf), "frag_color = vec4(vec3(texSample2D(g_Texture%d, v_TexCoord.xy).r), 1.0)",
                         debug_view_mode - 11);
                debug_line = buf;
            } else {
                debug_line = "frag_color = vec4(1,0,1,1)";
            }
            full_fs.replace(pos, end - pos + 1, debug_line + ";");
            effect_log.info("DEBUG: Overriding FS output with mode %d", debug_view_mode);
        }
    }
    return full_fs;
}

std::string ShaderCompiler::applyDebugStep(const std::string& shader_name, const std::string& fsSource,
                                           int debug_step) {
    if (debug_step == 0) return fsSource;

    std::string full_fs = fsSource;
    size_t main_pos = full_fs.find("void main()");
    if (main_pos != std::string::npos) {
        size_t body_start = full_fs.find('{', main_pos) + 1;
        int depth = 1;
        size_t body_end = body_start;
        while (body_end < full_fs.size() && depth > 0) {
            if (full_fs[body_end] == '{') depth++;
            if (full_fs[body_end] == '}') depth--;
            body_end++;
        }
        body_end--;
        std::string before = full_fs.substr(0, body_start);
        std::string after = full_fs.substr(body_end);

        const char* body = nullptr;
        char body_buf[512];

        if (shader_name.find("depthparallax") != std::string::npos) {
            switch (debug_step) {
                case 1:
                    body = "\n\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy);\n";
                    break;
                case 2:
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy);\n";
                    break;
                case 3:
                    body =
                        "\n\tfloat depth = 0.5;\n"
                        "\tfloat mask = 1.0;\n"
                        "\tvec2 pointer = vec2(v_TexCoord.z, 1.0 - v_TexCoord.w);\n"
                        "\tpointer = (pointer - v_ParallaxOffset) * vec2(2.0, -2.0) * g_Scale * "
                        "-Config::kDepthOffsetBase;\n"
                        "\tvec2 offset = (depth * 2.0 - 1.0) * pointer * mask;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                case 4:
                    body =
                        "\n\tvec2 offset = vec2(0.005, 0.005);\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                case 5:
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfrag_color = vec4(vec3(depth), 1.0);\n";
                    break;
                case 6:
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfloat mask = 1.0;\n"
                        "\tvec2 pointer = vec2(v_TexCoord.z, 1.0 - v_TexCoord.w);\n"
                        "\tpointer = (pointer - v_ParallaxOffset) * vec2(2.0, -2.0) * 1.0 * "
                        "-Config::kDepthOffsetBase;\n"
                        "\tvec2 offset = (depth * 2.0 - 1.0) * pointer * mask;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                case 7:
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfloat mask = 1.0;\n"
                        "#if MASK\n"
                        "\tmask *= texSample2D(g_Texture2, v_TexCoordMask.xy).r;\n"
                        "#endif\n"
                        "\tvec2 pointer = vec2(v_TexCoord.z, 1.0 - v_TexCoord.w);\n"
                        "\tpointer = (pointer - v_ParallaxOffset) * vec2(2.0, -2.0) * g_Scale * "
                        "-Config::kDepthOffsetBase;\n"
                        "\tvec2 offset = (depth * 2.0 - 1.0) * pointer * mask;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                default:
                    snprintf(body_buf, sizeof(body_buf), "\n\tfrag_color = texSample2D(g_Texture%d, v_TexCoord.xy);\n",
                             debug_step - 1);
                    body = body_buf;
                    break;
            }
        } else {
            snprintf(body_buf, sizeof(body_buf), "\n\tfrag_color = texSample2D(g_Texture%d, v_TexCoord.xy);\n",
                     debug_step - 1);
            body = body_buf;
        }

        full_fs = before + body + after;
        effect_log.info("DEBUG STEP %d: modified FS main()", debug_step);
    }
    return full_fs;
}
#endif
