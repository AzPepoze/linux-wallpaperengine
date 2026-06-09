#include "shader_compiler.h"

#include "../core/config.h"
#include "../core/logger.h"
#include "render.h"  // For builtin_uniforms_t

CompiledShader ShaderCompiler::compile(const std::string& shader_name, const std::string& vertSource,
                                       const std::string& fragSource,
                                       const std::map<std::string, std::vector<float>>& uniforms, int textureCount) {
    CompiledShader result;

    sg_shader_desc shd_desc = {};
    shd_desc.attrs[0].glsl_name = "a_Position";
    shd_desc.attrs[1].glsl_name = "a_TexCoord";
    shd_desc.vertex_func.source = vertSource.c_str();
    shd_desc.fragment_func.source = fragSource.c_str();

    // Slot 0: Built-in Uniforms (WPE style)
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrix";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[1].size = sizeof(builtin_uniforms_t) - sizeof(mat4x4);  // -mvp
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "g_Texture0Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "g_Texture1Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "g_Texture2Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "g_Texture3Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[4].glsl_name = "g_Texture4Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[5].glsl_name = "g_ParallaxPosition";
    shd_desc.uniform_blocks[1].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[6].glsl_name = "g_Time";
    shd_desc.uniform_blocks[1].glsl_uniforms[6].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[7].glsl_name = "g_Padding1";
    shd_desc.uniform_blocks[1].glsl_uniforms[7].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[8].glsl_name = "g_Screen";
    shd_desc.uniform_blocks[1].glsl_uniforms[8].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[9].glsl_name = "g_Padding2";
    shd_desc.uniform_blocks[1].glsl_uniforms[9].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[10].glsl_name = "g_EffectTextureProjectionMatrix";
    shd_desc.uniform_blocks[1].glsl_uniforms[10].type = SG_UNIFORMTYPE_MAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[11].glsl_name = "g_EffectTextureProjectionMatrixInverse";
    shd_desc.uniform_blocks[1].glsl_uniforms[11].type = SG_UNIFORMTYPE_MAT4;

    // Slot 2: Fragment Tint
    shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[2].size = sizeof(float) * 4;
    shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "tint";
    shd_desc.uniform_blocks[2].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    // Custom uniforms (up to 8 slots total in Sokol)
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;

        sg_uniform_type type = SG_UNIFORMTYPE_FLOAT4;
        if (vertSource.find("uniform float " + name) != std::string::npos ||
            fragSource.find("uniform float " + name) != std::string::npos) {
            type = SG_UNIFORMTYPE_FLOAT;
        } else if (vertSource.find("uniform vec2 " + name) != std::string::npos ||
                   fragSource.find("uniform vec2 " + name) != std::string::npos) {
            type = SG_UNIFORMTYPE_FLOAT2;
        } else if (vertSource.find("uniform vec3 " + name) != std::string::npos ||
                   fragSource.find("uniform vec3 " + name) != std::string::npos) {
            type = SG_UNIFORMTYPE_FLOAT3;
        }

        bool in_vs = vertSource.find(name) != std::string::npos;
        bool in_fs = fragSource.find(name) != std::string::npos;

        if (in_vs)
            shd_desc.uniform_blocks[u_idx].stage = SG_SHADERSTAGE_VERTEX;
        else
            shd_desc.uniform_blocks[u_idx].stage = SG_SHADERSTAGE_FRAGMENT;

        shd_desc.uniform_blocks[u_idx].size = 16;  // Sokol requires multiple of 16
        shd_desc.uniform_blocks[u_idx].glsl_uniforms[0].glsl_name = name.c_str();
        shd_desc.uniform_blocks[u_idx].glsl_uniforms[0].type = type;

        // Add padding to match 16-byte block size
        if (type == SG_UNIFORMTYPE_FLOAT) {
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[1].glsl_name = "dummy_pad_0";
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT;
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[2].glsl_name = "dummy_pad_1";
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[3].glsl_name = "dummy_pad_2";
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
        } else if (type == SG_UNIFORMTYPE_FLOAT2) {
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[1].glsl_name = "dummy_pad_0";
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
        } else if (type == SG_UNIFORMTYPE_FLOAT3) {
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[1].glsl_name = "dummy_pad_0";
            shd_desc.uniform_blocks[u_idx].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT;
        }
        u_idx++;
    }

    // Samplers
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].glsl_name = "g_Texture0";
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;

    for (int i = 0; i < textureCount && i < 11; i++) {
        char tex_name[32];
        snprintf(tex_name, sizeof(tex_name), "g_Texture%d", i + 1);
        shd_desc.views[i + 1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[i + 1].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.texture_sampler_pairs[i + 1].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[i + 1].glsl_name = tex_name;
        shd_desc.texture_sampler_pairs[i + 1].view_slot = i + 1;
        shd_desc.texture_sampler_pairs[i + 1].sampler_slot = 0;  // Use same sampler
    }

    result.shader = sg_make_shader(&shd_desc);

    if (result.shader.id == SG_INVALID_ID) {
        effect_log.error("Failed to create shader for %s", shader_name.c_str());
        return result;
    }

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = result.shader;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.cull_mode = SG_CULLMODE_NONE;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.stencil.enabled = false;

    result.pipeline = sg_make_pipeline(&pip_desc);

    if (result.pipeline.id == SG_INVALID_ID) {
        effect_log.error("Failed to create pipeline for %s", shader_name.c_str());
    } else {
        effect_log.info("Created pipeline for %s", shader_name.c_str());
    }

    return result;
}

std::string ShaderCompiler::applyDebugMode(const std::string& fsSource, int debug_view_mode) {
    if (debug_view_mode == 0) return fsSource;

    std::string full_fs = fsSource;
    size_t pos = full_fs.rfind("frag_color = ");
    if (pos != std::string::npos) {
        size_t end = full_fs.find(';', pos);
        if (end != std::string::npos) {
            std::string debug_line;
            if (debug_view_mode >= 1 && debug_view_mode <= 10) {
                // Show Texture N
                char buf[64];
                snprintf(buf, sizeof(buf), "frag_color = texSample2D(g_Texture%d, v_TexCoord.xy)", debug_view_mode - 1);
                debug_line = buf;
            } else if (debug_view_mode >= 11 && debug_view_mode <= 20) {
                // Show Texture N Red Channel (Grayscale)
                char buf[128];
                snprintf(buf, sizeof(buf), "frag_color = vec4(vec3(texSample2D(g_Texture%d, v_TexCoord.xy).r), 1.0)",
                         debug_view_mode - 11);
                debug_line = buf;
            } else {
                debug_line = "frag_color = vec4(1,0,1,1)";  // magenta for unknown
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
        // Find matching } by counting braces
        int depth = 1;
        size_t body_end = body_start;
        while (body_end < full_fs.size() && depth > 0) {
            if (full_fs[body_end] == '{') depth++;
            if (full_fs[body_end] == '}') depth--;
            body_end++;
        }
        body_end--;  // point to the matching }
        std::string before = full_fs.substr(0, body_start);
        std::string after = full_fs.substr(body_end);

        const char* body = nullptr;
        char body_buf[512];

        if (shader_name.find("depthparallax") != std::string::npos) {
            switch (debug_step) {
                case 1:  // just show g_Texture0 at original UV
                    body = "\n\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy);\n";
                    break;
                case 2:  // + sample depth (but don't use it for offset yet)
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy);\n";
                    break;
                case 3:  // + offset with neutral depth=0.5 (offset=0 from math, tests g_Scale+v_Parallax)
                    body =
                        "\n\tfloat depth = 0.5;\n"
                        "\tfloat mask = 1.0;\n"
                        "\tvec2 pointer = vec2(v_TexCoord.z, 1.0 - v_TexCoord.w);\n"
                        "\tpointer = (pointer - v_ParallaxOffset) * vec2(2.0, -2.0) * g_Scale * "
                        "-Config::kDepthOffsetBase;\n"
                        "\tvec2 offset = (depth * 2.0 - 1.0) * pointer * mask;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                case 4:  // + small fixed offset (0.005) to test if ANY offset blanks
                    body =
                        "\n\tvec2 offset = vec2(0.005, 0.005);\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                case 5:  // show depth as grayscale
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfrag_color = vec4(vec3(depth), 1.0);\n";
                    break;
                case 6:  // + hardcode g_Scale=1, real depth
                    body =
                        "\n\tfloat depth = texSample2D(g_Texture1, v_TexCoord.xy).r;\n"
                        "\tfloat mask = 1.0;\n"
                        "\tvec2 pointer = vec2(v_TexCoord.z, 1.0 - v_TexCoord.w);\n"
                        "\tpointer = (pointer - v_ParallaxOffset) * vec2(2.0, -2.0) * 1.0 * "
                        "-Config::kDepthOffsetBase;\n"
                        "\tvec2 offset = (depth * 2.0 - 1.0) * pointer * mask;\n"
                        "\tfrag_color = texSample2D(g_Texture0, v_TexCoord.xy + offset);\n";
                    break;
                case 7:  // + sample mask from g_Texture2
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
