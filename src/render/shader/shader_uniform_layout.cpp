#include "shader_uniform_layout.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "core/logger.h"

namespace {
enum class UniformType { Unknown, Float, Vec2, Vec3, Vec4, Int, IVec2, IVec3, IVec4, Bool };

struct UniformDecl {
    std::string name;
    UniformType type = UniformType::Unknown;
};

bool isTokenChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

UniformType parseUniformType(const std::string& declaration) {
    std::istringstream stream(declaration);
    std::string token;
    while (stream >> token) {
        if (token == "float") return UniformType::Float;
        if (token == "vec2" || token == "float2") return UniformType::Vec2;
        if (token == "vec3" || token == "float3") return UniformType::Vec3;
        if (token == "vec4" || token == "float4") return UniformType::Vec4;
        if (token == "int") return UniformType::Int;
        if (token == "ivec2" || token == "int2") return UniformType::IVec2;
        if (token == "ivec3" || token == "int3") return UniformType::IVec3;
        if (token == "ivec4" || token == "int4") return UniformType::IVec4;
        if (token == "bool") return UniformType::Bool;
    }
    return UniformType::Unknown;
}

bool findUniform(const std::string& source, const std::string& name, size_t from, size_t& start, size_t& end,
                 UniformType& type) {
    for (size_t name_pos = from; (name_pos = source.find(name, name_pos)) != std::string::npos;) {
        const size_t name_end = name_pos + name.size();
        if ((name_pos && isTokenChar(source[name_pos - 1])) ||
            (name_end < source.size() && isTokenChar(source[name_end]))) {
            name_pos = name_end;
            continue;
        }
        const size_t line_start_raw = source.rfind('\n', name_pos);
        const size_t line_start = line_start_raw == std::string::npos ? 0 : line_start_raw + 1;
        const size_t semicolon = source.find(';', name_end);
        const size_t line_end = source.find('\n', name_end);
        const size_t uniform_pos = source.find("uniform", line_start);
        if (semicolon == std::string::npos || (line_end != std::string::npos && semicolon > line_end) ||
            uniform_pos == std::string::npos || uniform_pos > name_pos || uniform_pos > semicolon) {
            name_pos = name_end;
            continue;
        }
        type = parseUniformType(source.substr(uniform_pos, semicolon - uniform_pos + 1));
        if (type == UniformType::Unknown) {
            name_pos = name_end;
            continue;
        }
        start = uniform_pos;
        end = semicolon + 1;
        return true;
    }
    return false;
}

std::string packedExpression(const std::string& packed_name, UniformType type) {
    switch (type) {
        case UniformType::Float:
            return "(" + packed_name + ".x)";
        case UniformType::Vec2:
            return "(" + packed_name + ".xy)";
        case UniformType::Vec3:
            return "(" + packed_name + ".xyz)";
        case UniformType::Vec4:
            return "(" + packed_name + ")";
        case UniformType::Int:
            return "int(" + packed_name + ".x)";
        case UniformType::IVec2:
            return "ivec2(" + packed_name + ".xy)";
        case UniformType::IVec3:
            return "ivec3(" + packed_name + ".xyz)";
        case UniformType::IVec4:
            return "ivec4(" + packed_name + ")";
        case UniformType::Bool:
            return "(" + packed_name + ".x != 0.0)";
        default:
            return "(" + packed_name + ")";
    }
}

void replaceUniformDeclaration(std::string& source, const UniformDecl& declaration, const std::string& packed_name) {
    bool emitted_alias = false;
    for (size_t search = 0;;) {
        size_t start = 0, end = 0;
        UniformType ignored = UniformType::Unknown;
        if (!findUniform(source, declaration.name, search, start, end, ignored)) return;
        const std::string replacement =
            emitted_alias ? "" : "#define " + declaration.name + " " + packedExpression(packed_name, declaration.type);
        emitted_alias = true;
        source.replace(start, end - start, replacement);
        search = start + replacement.size();
    }
}

void appendBlocks(const std::vector<UniformDecl>& declarations, sg_shader_stage stage, std::string& source,
                  sg_shader_desc& shader_desc, CompiledShader& result, int& next_slot,
                  std::string names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS]) {
    size_t declaration_index = 0;
    while (declaration_index < declarations.size() && next_slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS) {
        const int slot = next_slot++;
        const int count = (int)std::min(declarations.size() - declaration_index, (size_t)SG_MAX_UNIFORMBLOCK_MEMBERS);
        shader_desc.uniform_blocks[slot].stage = stage;
        shader_desc.uniform_blocks[slot].size = count * 16;
        CompiledUniformBlock block;
        block.slot = slot;
        for (int member = 0; member < count; ++member) {
            const UniformDecl& declaration = declarations[declaration_index + member];
            names[slot][member] = "_lwe_custom_" + std::to_string(slot) + "_" + std::to_string(member);
            shader_desc.uniform_blocks[slot].glsl_uniforms[member].glsl_name = names[slot][member].c_str();
            shader_desc.uniform_blocks[slot].glsl_uniforms[member].type = SG_UNIFORMTYPE_FLOAT4;
            replaceUniformDeclaration(source, declaration, names[slot][member]);
            block.uniform_names.push_back(declaration.name);
        }
        result.custom_uniform_blocks.push_back(std::move(block));
        declaration_index += count;
    }
    if (declaration_index < declarations.size())
        effect_log.warn("Custom uniform block capacity exceeded: %zu uniform(s) were not bound",
                        declarations.size() - declaration_index);
}
}  // namespace

void configureCustomUniformBlocks(const std::map<std::string, std::vector<float>>& uniforms, std::string& vertex_source,
                                  std::string& fragment_source, sg_shader_desc& shader_desc, CompiledShader& result,
                                  int& next_uniform_slot,
                                  std::string names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS]) {
    std::vector<UniformDecl> vertex_uniforms;
    std::vector<UniformDecl> fragment_uniforms;
    for (const auto& [name, values] : uniforms) {
        (void)values;
        size_t start = 0, end = 0;
        UniformType type = UniformType::Unknown;
        if (findUniform(vertex_source, name, 0, start, end, type)) vertex_uniforms.push_back({name, type});
        if (findUniform(fragment_source, name, 0, start, end, type)) fragment_uniforms.push_back({name, type});
    }
    appendBlocks(vertex_uniforms, SG_SHADERSTAGE_VERTEX, vertex_source, shader_desc, result, next_uniform_slot, names);
    appendBlocks(fragment_uniforms, SG_SHADERSTAGE_FRAGMENT, fragment_source, shader_desc, result, next_uniform_slot,
                 names);
}
