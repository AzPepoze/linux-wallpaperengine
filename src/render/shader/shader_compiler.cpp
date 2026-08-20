#include "shader_compiler.h"

#include <sstream>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/logger.h"
#include "render/render.h"  // For builtin_uniforms_t
#include "shader_backend.h"

namespace {
enum class CustomUniformType {
    Unknown,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Int,
    IVec2,
    IVec3,
    IVec4,
    Bool,
};

struct CustomUniformDecl {
    std::string name;
    CustomUniformType type = CustomUniformType::Unknown;
};

bool isTokenChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

CustomUniformType parseUniformType(const std::string& declaration) {
    std::istringstream stream(declaration);
    std::string token;
    while (stream >> token) {
        if (token == "float") return CustomUniformType::Float;
        if (token == "vec2" || token == "float2") return CustomUniformType::Vec2;
        if (token == "vec3" || token == "float3") return CustomUniformType::Vec3;
        if (token == "vec4" || token == "float4") return CustomUniformType::Vec4;
        if (token == "int") return CustomUniformType::Int;
        if (token == "ivec2" || token == "int2") return CustomUniformType::IVec2;
        if (token == "ivec3" || token == "int3") return CustomUniformType::IVec3;
        if (token == "ivec4" || token == "int4") return CustomUniformType::IVec4;
        if (token == "bool") return CustomUniformType::Bool;
    }
    return CustomUniformType::Unknown;
}

bool findUniformDeclaration(const std::string& source, const std::string& name, size_t from, size_t& start, size_t& end,
                            CustomUniformType& type) {
    size_t name_pos = from;
    while ((name_pos = source.find(name, name_pos)) != std::string::npos) {
        const bool left_ok = name_pos == 0 || !isTokenChar(source[name_pos - 1]);
        const size_t name_end = name_pos + name.size();
        const bool right_ok = name_end >= source.size() || !isTokenChar(source[name_end]);
        if (!left_ok || !right_ok) {
            name_pos = name_end;
            continue;
        }

        const size_t line_start_raw = source.rfind('\n', name_pos);
        const size_t line_start = line_start_raw == std::string::npos ? 0 : line_start_raw + 1;
        const size_t semicolon = source.find(';', name_end);
        const size_t line_end = source.find('\n', name_end);
        if (semicolon == std::string::npos || (line_end != std::string::npos && semicolon > line_end)) {
            name_pos = name_end;
            continue;
        }

        const size_t uniform_pos = source.find("uniform", line_start);
        if (uniform_pos == std::string::npos || uniform_pos > name_pos || uniform_pos > semicolon) {
            name_pos = name_end;
            continue;
        }

        const std::string declaration = source.substr(uniform_pos, semicolon - uniform_pos + 1);
        const CustomUniformType detected = parseUniformType(declaration);
        if (detected == CustomUniformType::Unknown) {
            name_pos = name_end;
            continue;
        }

        start = uniform_pos;
        end = semicolon + 1;
        type = detected;
        return true;
    }
    return false;
}

bool hasUniformDeclaration(const std::string& source, const std::string& name, CustomUniformType& type) {
    size_t start = 0, end = 0;
    return findUniformDeclaration(source, name, 0, start, end, type);
}

std::string packedExpression(const std::string& packed_name, CustomUniformType type) {
    switch (type) {
        case CustomUniformType::Float:
            return "(" + packed_name + ".x)";
        case CustomUniformType::Vec2:
            return "(" + packed_name + ".xy)";
        case CustomUniformType::Vec3:
            return "(" + packed_name + ".xyz)";
        case CustomUniformType::Vec4:
            return "(" + packed_name + ")";
        case CustomUniformType::Int:
            return "int(" + packed_name + ".x)";
        case CustomUniformType::IVec2:
            return "ivec2(" + packed_name + ".xy)";
        case CustomUniformType::IVec3:
            return "ivec3(" + packed_name + ".xyz)";
        case CustomUniformType::IVec4:
            return "ivec4(" + packed_name + ")";
        case CustomUniformType::Bool:
            return "(" + packed_name + ".x != 0.0)";
        default:
            return "(" + packed_name + ")";
    }
}

void rewriteUniformDeclaration(std::string& source, const std::string& name, const std::string& packed_name,
                               CustomUniformType type) {
    size_t search = 0;
    bool emitted_alias = false;
    while (true) {
        size_t start = 0, end = 0;
        CustomUniformType ignored = CustomUniformType::Unknown;
        if (!findUniformDeclaration(source, name, search, start, end, ignored)) break;

        std::string replacement;
        if (!emitted_alias) {
            replacement = "#define " + name + " " + packedExpression(packed_name, type);
            emitted_alias = true;
        }
        source.replace(start, end - start, replacement);
        search = start + replacement.size();
    }
}

void appendPackedBlocks(const std::vector<CustomUniformDecl>& declarations, sg_shader_stage stage, std::string& source,
                        sg_shader_desc& shd_desc, CompiledShader& result, int& next_slot,
                        std::string packed_names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS]) {
    size_t declaration_index = 0;
    while (declaration_index < declarations.size() && next_slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS) {
        const int slot = next_slot++;
        const size_t remaining = declarations.size() - declaration_index;
        const int member_count = (int)std::min(remaining, (size_t)SG_MAX_UNIFORMBLOCK_MEMBERS);

        shd_desc.uniform_blocks[slot].stage = stage;
        shd_desc.uniform_blocks[slot].size = member_count * 16;

        CompiledUniformBlock metadata;
        metadata.slot = slot;
        metadata.uniform_names.reserve(member_count);

        for (int member = 0; member < member_count; ++member) {
            const CustomUniformDecl& decl = declarations[declaration_index + member];
            packed_names[slot][member] = "_lwe_custom_" + std::to_string(slot) + "_" + std::to_string(member);
            shd_desc.uniform_blocks[slot].glsl_uniforms[member].glsl_name = packed_names[slot][member].c_str();
            shd_desc.uniform_blocks[slot].glsl_uniforms[member].type = SG_UNIFORMTYPE_FLOAT4;
            rewriteUniformDeclaration(source, decl.name, packed_names[slot][member], decl.type);
            metadata.uniform_names.push_back(decl.name);
        }

        result.custom_uniform_blocks.push_back(std::move(metadata));
        declaration_index += member_count;
    }

    if (declaration_index < declarations.size()) {
        effect_log.warn("Custom uniform block capacity exceeded: %zu uniform(s) were not bound",
                        declarations.size() - declaration_index);
    }
}
}  // namespace

CompiledShader ShaderCompiler::compile(const std::string& shader_name, const std::string& vertSource,
                                       const std::string& fragSource,
                                       const std::map<std::string, std::vector<float>>& uniforms, int textureCount) {
    CompiledShader result;
    std::string compiled_vert_source = vertSource;
    std::string compiled_frag_source = fragSource;

    sg_shader_desc shd_desc = {};
    shd_desc.attrs[0].glsl_name = "a_Position";
    shd_desc.attrs[1].glsl_name = "a_TexCoord";

    // Slot 0: MVP used by vertex shaders.
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrix";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    // Slot 1: shared Wallpaper Engine built-ins for the vertex stage.
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

    // Slot 2: the same built-ins for fragment shaders plus tint. Sokol uniform blocks are
    // stage-specific, so effects such as waterwaves need their own fragment-visible binding.
    shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[2].size = sizeof(builtin_uniforms_t) - sizeof(mat4x4) + sizeof(float) * 4;
    shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "g_Texture0Resolution";
    shd_desc.uniform_blocks[2].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[1].glsl_name = "g_Texture1Resolution";
    shd_desc.uniform_blocks[2].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[2].glsl_name = "g_Texture2Resolution";
    shd_desc.uniform_blocks[2].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[3].glsl_name = "g_Texture3Resolution";
    shd_desc.uniform_blocks[2].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[4].glsl_name = "g_Texture4Resolution";
    shd_desc.uniform_blocks[2].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[5].glsl_name = "g_ParallaxPosition";
    shd_desc.uniform_blocks[2].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[2].glsl_uniforms[6].glsl_name = "g_Time";
    shd_desc.uniform_blocks[2].glsl_uniforms[6].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[2].glsl_uniforms[7].glsl_name = "g_Padding1";
    shd_desc.uniform_blocks[2].glsl_uniforms[7].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[2].glsl_uniforms[8].glsl_name = "g_Screen";
    shd_desc.uniform_blocks[2].glsl_uniforms[8].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[2].glsl_uniforms[9].glsl_name = "g_Padding2";
    shd_desc.uniform_blocks[2].glsl_uniforms[9].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[2].glsl_uniforms[10].glsl_name = "g_EffectTextureProjectionMatrix";
    shd_desc.uniform_blocks[2].glsl_uniforms[10].type = SG_UNIFORMTYPE_MAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[11].glsl_name = "g_EffectTextureProjectionMatrixInverse";
    shd_desc.uniform_blocks[2].glsl_uniforms[11].type = SG_UNIFORMTYPE_MAT4;
    shd_desc.uniform_blocks[2].glsl_uniforms[12].glsl_name = "tint";
    shd_desc.uniform_blocks[2].glsl_uniforms[12].type = SG_UNIFORMTYPE_FLOAT4;

    // Pack authored effect uniforms into vec4-backed uniform blocks. This avoids
    // consuming one Sokol bind slot per value (stock effects routinely have more
    // than five constants) while keeping a predictable 16-byte ABI per uniform.
    std::vector<CustomUniformDecl> vertex_uniforms;
    std::vector<CustomUniformDecl> fragment_uniforms;
    for (const auto& [name, values] : uniforms) {
        (void)values;
        CustomUniformType type = CustomUniformType::Unknown;
        if (hasUniformDeclaration(compiled_vert_source, name, type)) vertex_uniforms.push_back({name, type});
        if (hasUniformDeclaration(compiled_frag_source, name, type)) fragment_uniforms.push_back({name, type});
    }

    std::string packed_names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS];
    int next_uniform_slot = 3;
    appendPackedBlocks(vertex_uniforms, SG_SHADERSTAGE_VERTEX, compiled_vert_source, shd_desc, result,
                       next_uniform_slot, packed_names);
    appendPackedBlocks(fragment_uniforms, SG_SHADERSTAGE_FRAGMENT, compiled_frag_source, shd_desc, result,
                       next_uniform_slot, packed_names);

    static const char* kTextureNames[] = {"g_Texture0", "g_Texture1", "g_Texture2",  "g_Texture3",
                                          "g_Texture4", "g_Texture5", "g_Texture6",  "g_Texture7",
                                          "g_Texture8", "g_Texture9", "g_Texture10", "g_Texture11"};

    // Samplers
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].glsl_name = kTextureNames[0];
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;

    for (int i = 0; i < textureCount && i < 11; i++) {
        const int slot = i + 1;
        shd_desc.views[slot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[slot].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.texture_sampler_pairs[slot].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[slot].glsl_name = kTextureNames[slot];
        shd_desc.texture_sampler_pairs[slot].view_slot = slot;
        shd_desc.texture_sampler_pairs[slot].sampler_slot = 0;  // Use same sampler
    }

    result.shader =
        create_backend_shader(&shd_desc, compiled_vert_source, compiled_frag_source, shader_name.c_str());

    if (result.shader.id == SG_INVALID_ID) {
        effect_log.error("Failed to create shader for %s", shader_name.c_str());
        return result;
    }

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = result.shader;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    // Effect passes render into a freshly-cleared ping-pong target and should
    // write their shader output directly. Layer blending happens when the final
    // image is composed into the scene, not between image-effect passes.
    pip_desc.colors[0].blend.enabled = false;
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
