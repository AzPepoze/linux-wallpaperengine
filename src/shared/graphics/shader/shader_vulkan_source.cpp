#include <cctype>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

#include "shader_backend_internal.h"
#include "shared/core/logger.h"

namespace {
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

const char* texture_type_to_slang(sg_image_type type) {
    switch (type) {
        case SG_IMAGETYPE_2D:
            return "Texture2D";
        case SG_IMAGETYPE_CUBE:
            return "TextureCube";
        case SG_IMAGETYPE_ARRAY:
            return "Texture2DArray";
        case SG_IMAGETYPE_3D:
            return "Texture3D";
        default:
            return nullptr;
    }
}

const char* combined_sampler_type(sg_image_type type) {
    switch (type) {
        case SG_IMAGETYPE_2D:
            return "LweCombinedSampler2D";
        case SG_IMAGETYPE_CUBE:
            return "LweCombinedSamplerCube";
        case SG_IMAGETYPE_ARRAY:
            return "LweCombinedSampler2DArray";
        case SG_IMAGETYPE_3D:
            return "LweCombinedSampler3D";
        default:
            return nullptr;
    }
}

void emit_sampler_adapter(std::string& declarations, sg_image_type type, bool& emitted_2d, bool& emitted_cube,
                          bool& emitted_array, bool& emitted_3d) {
    switch (type) {
        case SG_IMAGETYPE_2D:
            if (emitted_2d) return;
            emitted_2d = true;
            declarations +=
                "struct LweCombinedSampler2D { Texture2D t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSampler2D sampler, vec2 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
                "vec4 texSample2D(LweCombinedSampler2D sampler, vec2 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
                "vec4 texture2D(LweCombinedSampler2D sampler, vec2 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
                "vec4 textureLod(LweCombinedSampler2D sampler, vec2 uv, float lod) { return "
                "sampler.t.SampleLevel(sampler.s, uv, lod); }\n"
                "vec4 texSample2DLod(LweCombinedSampler2D sampler, vec2 uv, float lod) { return "
                "sampler.t.SampleLevel(sampler.s, uv, lod); }\n"
                "vec4 texture2DLod(LweCombinedSampler2D sampler, vec2 uv, float lod) { return "
                "sampler.t.SampleLevel(sampler.s, uv, lod); }\n"
                "vec4 textureGrad(LweCombinedSampler2D sampler, vec2 uv, vec2 dx, vec2 dy) { return "
                "sampler.t.SampleGrad(sampler.s, uv, dx, dy); }\n"
                "vec4 texSample2DGrad(LweCombinedSampler2D sampler, vec2 uv, vec2 dx, vec2 dy) { return "
                "sampler.t.SampleGrad(sampler.s, uv, dx, dy); }\n";
            return;
        case SG_IMAGETYPE_CUBE:
            if (emitted_cube) return;
            emitted_cube = true;
            declarations +=
                "struct LweCombinedSamplerCube { TextureCube t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSamplerCube sampler, vec3 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
                "vec4 textureLod(LweCombinedSamplerCube sampler, vec3 uv, float lod) { return "
                "sampler.t.SampleLevel(sampler.s, uv, lod); }\n";
            return;
        case SG_IMAGETYPE_ARRAY:
            if (emitted_array) return;
            emitted_array = true;
            declarations +=
                "struct LweCombinedSampler2DArray { Texture2DArray t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSampler2DArray sampler, vec3 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
                "vec4 textureLod(LweCombinedSampler2DArray sampler, vec3 uv, float lod) { return "
                "sampler.t.SampleLevel(sampler.s, uv, lod); }\n";
            return;
        case SG_IMAGETYPE_3D:
            if (emitted_3d) return;
            emitted_3d = true;
            declarations +=
                "struct LweCombinedSampler3D { Texture3D t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSampler3D sampler, vec3 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
                "vec4 textureLod(LweCombinedSampler3D sampler, vec3 uv, float lod) { return "
                "sampler.t.SampleLevel(sampler.s, uv, lod); }\n";
            return;
        default:
            return;
    }
}

void remove_uniform_declaration(std::string& source, const char* name) {
    if (!name || !name[0]) return;

    const auto is_token_char = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    size_t search_pos = 0;
    const size_t name_len = std::strlen(name);
    while (true) {
        const size_t name_pos = source.find(name, search_pos);
        if (name_pos == std::string::npos) break;

        const size_t name_end = name_pos + name_len;
        // Names that merely share a prefix (g_PointerPosition vs
        // g_PointerPositionLast) must not match, otherwise an unrelated
        // declaration is erased and the uniform becomes undefined.
        if ((name_pos > 0 && is_token_char(source[name_pos - 1])) ||
            (name_end < source.size() && is_token_char(source[name_end]))) {
            search_pos = name_end;
            continue;
        }

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

bool parse_varying_line(const std::string& line, const char* direction, std::string& name) {
    std::string source = line;
    const size_t comment = source.find("//");
    if (comment != std::string::npos) source.erase(comment);

    std::istringstream stream(source);
    std::string first;
    if (!(stream >> first)) return false;
    if (first == "flat" || first == "smooth" || first == "noperspective") {
        if (!(stream >> first)) return false;
    }
    if (first != direction) return false;

    std::string type;
    if (!(stream >> type >> name)) return false;
    const size_t terminator = name.find_first_of(";[");
    if (terminator != std::string::npos) name.erase(terminator);
    return !name.empty();
}

void collect_varyings(const std::string& source, const char* direction, std::map<std::string, int>& locations,
                      int& next_location) {
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        std::string name;
        if (!parse_varying_line(line, direction, name) || locations.count(name)) continue;
        locations[name] = next_location++;
    }
}

std::string apply_varying_locations(const std::string& source, const char* direction,
                                    const std::map<std::string, int>& locations) {
    std::istringstream lines(source);
    std::string result;
    std::string line;
    while (std::getline(lines, line)) {
        std::string name;
        if (parse_varying_line(line, direction, name)) {
            const auto location = locations.find(name);
            if (location != locations.end() && line.find("layout(") == std::string::npos) {
                const size_t first = line.find_first_not_of(" \t");
                const size_t insert_at = first == std::string::npos ? 0 : first;
                line.insert(insert_at, "layout(location = " + std::to_string(location->second) + ") ");
            }
        }
        result += line;
        result += '\n';
    }
    return result;
}
}  // namespace

namespace shader_backend_internal {
void assign_matching_varying_locations(std::string& vertex_source, std::string& fragment_source) {
    // Slang assigns stage-interface locations independently; give shared varyings explicit locations.
    std::map<std::string, int> locations;
    int next_location = 0;
    collect_varyings(vertex_source, "out", locations, next_location);
    collect_varyings(fragment_source, "in", locations, next_location);
    vertex_source = apply_varying_locations(vertex_source, "out", locations);
    fragment_source = apply_varying_locations(fragment_source, "in", locations);
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

    if (stage == SG_SHADERSTAGE_VERTEX) {
        // Slang numbers vertex inputs by declaration order, but the pipeline layout follows desc.attrs.
        std::map<std::string, int> attribute_locations;
        for (int slot = 0; slot < SG_MAX_VERTEX_ATTRIBUTES; ++slot) {
            const char* attribute_name = desc.attrs[slot].glsl_name;
            if (attribute_name && attribute_name[0]) attribute_locations.emplace(attribute_name, slot);
        }
        source = apply_varying_locations(source, "in", attribute_locations);
    }

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
        declarations += "};\n";

        if (!has_member) {
            core_log.error("Vulkan uniform block %d has no GLSL member metadata", slot);
        }
    }

    bool emitted_views[SG_MAX_VIEW_BINDSLOTS] = {};
    bool emitted_samplers[SG_MAX_SAMPLER_BINDSLOTS] = {};
    bool emitted_adapter_2d = false;
    bool emitted_adapter_cube = false;
    bool emitted_adapter_array = false;
    bool emitted_adapter_3d = false;
    const size_t pair_count = sizeof(desc.texture_sampler_pairs) / sizeof(desc.texture_sampler_pairs[0]);
    for (size_t pair_slot = 0; pair_slot < pair_count; ++pair_slot) {
        const sg_shader_texture_sampler_pair& pair = desc.texture_sampler_pairs[pair_slot];
        if (pair.glsl_name && pair.glsl_name[0]) remove_uniform_declaration(source, pair.glsl_name);
        if (pair.stage != stage || !pair.glsl_name || !pair.glsl_name[0]) continue;
        if (pair.view_slot >= SG_MAX_VIEW_BINDSLOTS || pair.sampler_slot >= SG_MAX_SAMPLER_BINDSLOTS) continue;

        const sg_shader_texture_view& texture = desc.views[pair.view_slot].texture;
        const char* texture_type = texture_type_to_slang(texture.image_type);
        const char* adapter_type = combined_sampler_type(texture.image_type);
        if (!texture_type || !adapter_type) {
            core_log.error("Unsupported Vulkan image type for shader texture '%s'", pair.glsl_name);
            continue;
        }

        emit_sampler_adapter(declarations, texture.image_type, emitted_adapter_2d, emitted_adapter_cube,
                             emitted_adapter_array, emitted_adapter_3d);

        if (!emitted_views[pair.view_slot]) {
            declarations += "layout(set = 1, binding = " + std::to_string(pair.view_slot) + ") ";
            declarations += texture_type;
            declarations += " _lwe_view" + std::to_string(pair.view_slot) + ";\n";
            emitted_views[pair.view_slot] = true;
        }

        if (!emitted_samplers[pair.sampler_slot]) {
            const int sampler_binding = SG_MAX_VIEW_BINDSLOTS + pair.sampler_slot;
            declarations += "layout(set = 1, binding = " + std::to_string(sampler_binding) +
                            ") SamplerState _lwe_sampler" + std::to_string(pair.sampler_slot) + ";\n";
            emitted_samplers[pair.sampler_slot] = true;
        }

        declarations += "#define ";
        declarations += pair.glsl_name;
        declarations += " ";
        declarations += adapter_type;
        declarations += "(_lwe_view" + std::to_string(pair.view_slot) + ", _lwe_sampler" +
                        std::to_string(pair.sampler_slot) + ")\n";
    }

    return "#version 450\n" + declarations + source;
}
}  // namespace shader_backend_internal
