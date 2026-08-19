#include "shader_backend.h"

#include <cstring>
#include <string>
#include <vector>

#include "../core/logger.h"

#ifdef LWE_SOKOL_VULKAN
#include <shaderc/shaderc.h>
#endif

namespace {

#ifdef LWE_SOKOL_VULKAN

const char* uniform_type_to_glsl(sg_uniform_type type) {
    switch (type) {
        case SG_UNIFORMTYPE_FLOAT:
            return "float";
        case SG_UNIFORMTYPE_FLOAT2:
            return "vec2";
        case SG_UNIFORMTYPE_FLOAT3:
            return "vec3";
        case SG_UNIFORMTYPE_FLOAT4:
            return "vec4";
        case SG_UNIFORMTYPE_INT:
            return "int";
        case SG_UNIFORMTYPE_INT2:
            return "ivec2";
        case SG_UNIFORMTYPE_INT3:
            return "ivec3";
        case SG_UNIFORMTYPE_INT4:
            return "ivec4";
        case SG_UNIFORMTYPE_MAT4:
            return "mat4";
        default:
            return nullptr;
    }
}

const char* texture_type_to_glsl(sg_image_type type) {
    switch (type) {
        case SG_IMAGETYPE_2D:
            return "texture2D";
        case SG_IMAGETYPE_CUBE:
            return "textureCube";
        case SG_IMAGETYPE_ARRAY:
            return "texture2DArray";
        case SG_IMAGETYPE_3D:
            return "texture3D";
        default:
            return nullptr;
    }
}

const char* sampler_constructor_to_glsl(sg_image_type type) {
    switch (type) {
        case SG_IMAGETYPE_2D:
            return "sampler2D";
        case SG_IMAGETYPE_CUBE:
            return "samplerCube";
        case SG_IMAGETYPE_ARRAY:
            return "sampler2DArray";
        case SG_IMAGETYPE_3D:
            return "sampler3D";
        default:
            return nullptr;
    }
}

void remove_uniform_declaration(std::string& source, const char* name) {
    if (!name || !name[0]) return;

    size_t search_pos = 0;
    const size_t name_len = std::strlen(name);
    while (true) {
        const size_t name_pos = source.find(name, search_pos);
        if (name_pos == std::string::npos) break;

        const size_t previous_newline = source.rfind('\n', name_pos);
        const size_t line_start = previous_newline == std::string::npos ? 0 : previous_newline + 1;
        const size_t line_end = source.find('\n', name_pos);
        const size_t uniform_pos = source.find("uniform", line_start);
        if (uniform_pos != std::string::npos && uniform_pos < name_pos &&
            (line_end == std::string::npos || uniform_pos < line_end)) {
            const size_t semicolon = source.find(';', name_pos);
            if (semicolon != std::string::npos && (line_end == std::string::npos || semicolon < line_end)) {
                source.erase(uniform_pos, semicolon - uniform_pos + 1);
                search_pos = uniform_pos;
                continue;
            }
        }
        search_pos = name_pos + name_len;
    }
}

void remove_precision_statement(std::string& source) {
    size_t search_pos = 0;
    while (true) {
        const size_t precision_pos = source.find("precision ", search_pos);
        if (precision_pos == std::string::npos) break;
        const size_t semicolon = source.find(';', precision_pos);
        if (semicolon == std::string::npos) break;
        source.erase(precision_pos, semicolon - precision_pos + 1);
        search_pos = precision_pos;
    }
}

std::string strip_version(std::string source) {
    const size_t version_pos = source.find("#version");
    if (version_pos != std::string::npos) {
        const size_t version_end = source.find('\n', version_pos);
        source.erase(version_pos,
                     version_end == std::string::npos ? source.size() - version_pos : version_end - version_pos + 1);
    }
    return source;
}

bool prepare_vulkan_bindings(sg_shader_desc* desc) {
    for (int slot = 0; slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++slot) {
        if (desc->uniform_blocks[slot].stage != SG_SHADERSTAGE_NONE) {
            desc->uniform_blocks[slot].spirv_set0_binding_n = static_cast<uint8_t>(slot);
        }
    }

    for (int slot = 0; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
        if (desc->views[slot].texture.stage != SG_SHADERSTAGE_NONE) {
            desc->views[slot].texture.spirv_set1_binding_n = static_cast<uint8_t>(slot);
        }
    }

    for (int slot = 0; slot < SG_MAX_SAMPLER_BINDSLOTS; ++slot) {
        if (desc->samplers[slot].stage != SG_SHADERSTAGE_NONE) {
            const int binding = SG_MAX_VIEW_BINDSLOTS + slot;
            if (binding > 127) {
                core_log.error("Vulkan sampler binding %d exceeds Sokol's SPIR-V binding range", binding);
                return false;
            }
            desc->samplers[slot].spirv_set1_binding_n = static_cast<uint8_t>(binding);
        }
    }
    return true;
}

std::string make_vulkan_source(const sg_shader_desc& desc, const std::string& original, sg_shader_stage stage) {
    std::string source = strip_version(original);
    remove_precision_statement(source);

    std::string declarations;

    for (int slot = 0; slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++slot) {
        const sg_shader_uniform_block& block = desc.uniform_blocks[slot];

        for (int member = 0; member < SG_MAX_UNIFORMBLOCK_MEMBERS; ++member) {
            const char* member_name = block.glsl_uniforms[member].glsl_name;
            if (member_name && member_name[0]) remove_uniform_declaration(source, member_name);
        }

        if (block.stage != stage) continue;

        declarations += "layout(std140, set = 0, binding = " + std::to_string(slot) + ") uniform LweUniformBlock" +
                        std::to_string(slot) + " {\n";
        bool has_member = false;
        for (int member = 0; member < SG_MAX_UNIFORMBLOCK_MEMBERS; ++member) {
            const sg_glsl_shader_uniform& uniform = block.glsl_uniforms[member];
            if (!uniform.glsl_name || !uniform.glsl_name[0]) break;
            const char* glsl_type = uniform_type_to_glsl(uniform.type);
            if (!glsl_type) {
                core_log.error("Unsupported Vulkan uniform type in block %d", slot);
                continue;
            }
            has_member = true;
            declarations += "    ";
            declarations += glsl_type;
            declarations += " ";
            declarations += uniform.glsl_name;
            if (uniform.array_count > 1) {
                declarations += "[" + std::to_string(uniform.array_count) + "]";
            }
            declarations += ";\n";
        }
        declarations += "} _lwe_ub" + std::to_string(slot) + ";\n";

        if (!has_member) {
            core_log.error("Vulkan uniform block %d has no GLSL member metadata", slot);
        }

        for (int member = 0; member < SG_MAX_UNIFORMBLOCK_MEMBERS; ++member) {
            const char* member_name = block.glsl_uniforms[member].glsl_name;
            if (!member_name || !member_name[0]) break;
            if (std::strncmp(member_name, "dummy_pad_", 10) == 0) continue;
            declarations += "#define ";
            declarations += member_name;
            declarations += " _lwe_ub" + std::to_string(slot) + "." + member_name + "\n";
        }
    }

    bool emitted_views[SG_MAX_VIEW_BINDSLOTS] = {};
    bool emitted_samplers[SG_MAX_SAMPLER_BINDSLOTS] = {};
    const size_t pair_count = sizeof(desc.texture_sampler_pairs) / sizeof(desc.texture_sampler_pairs[0]);
    for (size_t pair_slot = 0; pair_slot < pair_count; ++pair_slot) {
        const sg_shader_texture_sampler_pair& pair = desc.texture_sampler_pairs[pair_slot];
        if (pair.glsl_name && pair.glsl_name[0]) remove_uniform_declaration(source, pair.glsl_name);
        if (pair.stage != stage || !pair.glsl_name || !pair.glsl_name[0]) continue;
        if (pair.view_slot >= SG_MAX_VIEW_BINDSLOTS || pair.sampler_slot >= SG_MAX_SAMPLER_BINDSLOTS) continue;

        const sg_shader_texture_view& texture = desc.views[pair.view_slot].texture;
        const char* texture_type = texture_type_to_glsl(texture.image_type);
        const char* sampler_constructor = sampler_constructor_to_glsl(texture.image_type);
        if (!texture_type || !sampler_constructor) {
            core_log.error("Unsupported Vulkan image type for shader texture '%s'", pair.glsl_name);
            continue;
        }

        if (!emitted_views[pair.view_slot]) {
            declarations += "layout(set = 1, binding = " + std::to_string(pair.view_slot) + ") uniform ";
            declarations += texture_type;
            declarations += " _lwe_view" + std::to_string(pair.view_slot) + ";\n";
            emitted_views[pair.view_slot] = true;
        }

        if (!emitted_samplers[pair.sampler_slot]) {
            const int sampler_binding = SG_MAX_VIEW_BINDSLOTS + pair.sampler_slot;
            declarations += "layout(set = 1, binding = " + std::to_string(sampler_binding) +
                            ") uniform sampler _lwe_sampler" + std::to_string(pair.sampler_slot) + ";\n";
            emitted_samplers[pair.sampler_slot] = true;
        }

        declarations += "#define ";
        declarations += pair.glsl_name;
        declarations += " ";
        declarations += sampler_constructor;
        declarations += "(_lwe_view" + std::to_string(pair.view_slot) + ", _lwe_sampler" +
                        std::to_string(pair.sampler_slot) + ")\n";
    }

    return "#version 450\n" + declarations + source;
}

bool compile_spirv(shaderc_compiler_t compiler, shaderc_shader_kind kind, const std::string& source,
                   const char* source_name, std::vector<uint32_t>& output) {
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if (!options) {
        core_log.error("Failed to initialize shaderc compile options");
        return false;
    }

    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_target_spirv(options, shaderc_spirv_version_1_0);
    shaderc_compile_options_set_auto_map_locations(options, true);
    shaderc_compile_options_set_vulkan_rules_relaxed(options, true);
#ifdef NDEBUG
    shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
#else
    shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_zero);
#endif

    shaderc_compilation_result_t result =
        shaderc_compile_into_spv(compiler, source.c_str(), source.size(), kind, source_name, "main", options);
    shaderc_compile_options_release(options);

    if (!result) {
        core_log.error("shaderc returned no result for %s", source_name);
        return false;
    }

    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        core_log.error("Vulkan shader compilation failed for %s: %s", source_name,
                       shaderc_result_get_error_message(result));
        shaderc_result_release(result);
        return false;
    }

    const size_t byte_length = shaderc_result_get_length(result);
    if (byte_length == 0 || (byte_length % sizeof(uint32_t)) != 0) {
        core_log.error("shaderc produced invalid SPIR-V length for %s", source_name);
        shaderc_result_release(result);
        return false;
    }

    output.resize(byte_length / sizeof(uint32_t));
    std::memcpy(output.data(), shaderc_result_get_bytes(result), byte_length);
    shaderc_result_release(result);
    return true;
}

#endif  // LWE_SOKOL_VULKAN

}  // namespace

sg_shader create_backend_shader(sg_shader_desc* desc, const std::string& vertex_source,
                                const std::string& fragment_source, const char* label) {
    if (!desc) return {SG_INVALID_ID};
    if (label) desc->label = label;

#ifdef LWE_SOKOL_VULKAN
    if (!prepare_vulkan_bindings(desc)) return {SG_INVALID_ID};

    const std::string vulkan_vs = make_vulkan_source(*desc, vertex_source, SG_SHADERSTAGE_VERTEX);
    const std::string vulkan_fs = make_vulkan_source(*desc, fragment_source, SG_SHADERSTAGE_FRAGMENT);

    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    if (!compiler) {
        core_log.error("Failed to initialize shaderc compiler");
        return {SG_INVALID_ID};
    }

    std::vector<uint32_t> vertex_spirv;
    std::vector<uint32_t> fragment_spirv;
    const std::string vertex_name = std::string(label ? label : "shader") + ".vert";
    const std::string fragment_name = std::string(label ? label : "shader") + ".frag";

    const bool vertex_ok =
        compile_spirv(compiler, shaderc_glsl_vertex_shader, vulkan_vs, vertex_name.c_str(), vertex_spirv);
    const bool fragment_ok =
        compile_spirv(compiler, shaderc_glsl_fragment_shader, vulkan_fs, fragment_name.c_str(), fragment_spirv);
    shaderc_compiler_release(compiler);

    if (!vertex_ok || !fragment_ok) return {SG_INVALID_ID};

    desc->vertex_func.source = nullptr;
    desc->fragment_func.source = nullptr;
    desc->vertex_func.bytecode = {vertex_spirv.data(), vertex_spirv.size() * sizeof(uint32_t)};
    desc->fragment_func.bytecode = {fragment_spirv.data(), fragment_spirv.size() * sizeof(uint32_t)};
    desc->vertex_func.entry = "main";
    desc->fragment_func.entry = "main";

    return sg_make_shader(desc);
#else
    desc->vertex_func.source = vertex_source.c_str();
    desc->fragment_func.source = fragment_source.c_str();
    return sg_make_shader(desc);
#endif
}
