#include "shader_backend.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <atomic>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/logger.h"

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
                "vec4 texture2D(LweCombinedSampler2D sampler, vec2 uv) { return sampler.t.Sample(sampler.s, uv); }\n";
            return;
        case SG_IMAGETYPE_CUBE:
            if (emitted_cube) return;
            emitted_cube = true;
            declarations +=
                "struct LweCombinedSamplerCube { TextureCube t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSamplerCube sampler, vec3 uv) { return sampler.t.Sample(sampler.s, uv); }\n";
            return;
        case SG_IMAGETYPE_ARRAY:
            if (emitted_array) return;
            emitted_array = true;
            declarations +=
                "struct LweCombinedSampler2DArray { Texture2DArray t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSampler2DArray sampler, vec3 uv) { return sampler.t.Sample(sampler.s, uv); "
                "}\n";
            return;
        case SG_IMAGETYPE_3D:
            if (emitted_3d) return;
            emitted_3d = true;
            declarations +=
                "struct LweCombinedSampler3D { Texture3D t; SamplerState s; };\n"
                "vec4 texture(LweCombinedSampler3D sampler, vec3 uv) { return sampler.t.Sample(sampler.s, uv); }\n";
            return;
        default:
            return;
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

void assign_matching_varying_locations(std::string& vertex_source, std::string& fragment_source) {
    // Vulkan links stage interfaces by location. GLSL source usually relies on
    // matching names, but Slang assigns locations independently per stage.
    // Give every interface varying a shared explicit location before compiling.
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

    std::string dummy_uses;
    for (int slot = 0; slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++slot) {
        const sg_shader_uniform_block& block = desc.uniform_blocks[slot];
        if (block.stage != stage) continue;
        const char* first_member = block.glsl_uniforms[0].glsl_name;
        if (first_member && first_member[0]) {
            dummy_uses += "    (void)" + std::string(first_member) + ";\n";
        }
    }
    if (!dummy_uses.empty()) {
        declarations += "void _lwe_retain_uniforms() {\n" + dummy_uses + "}\n";
        size_t main_pos = source.find("main()");
        if (main_pos != std::string::npos) {
            size_t brace_pos = source.find('{', main_pos);
            if (brace_pos != std::string::npos) {
                source.insert(brace_pos + 1, "\n    _lwe_retain_uniforms();\n");
            }
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

struct SlangCompilerContext {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    Slang::ComPtr<slang::ISession> session;

    bool initialize() {
        if (session) return true;

        SlangGlobalSessionDesc global_desc = {};
        global_desc.enableGLSL = true;
        if (slang_createGlobalSession2(&global_desc, global_session.writeRef()) != SLANG_OK || !global_session) {
            core_log.error("Failed to initialize Slang global session");
            return false;
        }

        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        target_desc.profile = global_session->findProfile("glsl_450");

        slang::SessionDesc session_desc = {};
        session_desc.targetCount = 1;
        session_desc.targets = &target_desc;
        session_desc.allowGLSLSyntax = true;
        if (global_session->createSession(session_desc, session.writeRef()) != SLANG_OK || !session) {
            core_log.error("Failed to initialize Slang GLSL-to-SPIR-V session");
            return false;
        }
        return true;
    }
};

SlangCompilerContext& slang_context() {
    static SlangCompilerContext context;
    return context;
}

std::atomic<uint64_t> slang_module_counter{0};

void log_slang_diagnostics(const char* source_name, slang::IBlob* diagnostics) {
    if (!diagnostics || !diagnostics->getBufferPointer() || diagnostics->getBufferSize() == 0) return;
    std::string text(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
    core_log.error("Slang diagnostics for %s: %s", source_name, text.c_str());
}

bool compile_spirv(SlangStage stage, const std::string& source, const char* source_name,
                   std::vector<uint32_t>& output) {
    SlangCompilerContext& context = slang_context();
    if (!context.initialize()) return false;

    const uint64_t module_id = slang_module_counter.fetch_add(1, std::memory_order_relaxed);
    const std::string module_name = "lwe_runtime_shader_" + std::to_string(module_id);
    const std::string virtual_source_name = module_name + "/" + source_name;

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = context.session->loadModuleFromSourceString(
        module_name.c_str(), virtual_source_name.c_str(), source.c_str(), diagnostics.writeRef());
    if (!module) {
        log_slang_diagnostics(source_name, diagnostics.get());
        core_log.error("Slang failed to load GLSL module for %s", source_name);
        return false;
    }

    Slang::ComPtr<slang::IEntryPoint> entry_point;
    diagnostics.setNull();
    if (module->findAndCheckEntryPoint("main", stage, entry_point.writeRef(), diagnostics.writeRef()) != SLANG_OK ||
        !entry_point) {
        log_slang_diagnostics(source_name, diagnostics.get());
        core_log.error("Slang failed to resolve main() for %s", source_name);
        return false;
    }

    slang::IComponentType* components[2] = {module, entry_point.get()};
    Slang::ComPtr<slang::IComponentType> composed_program;
    diagnostics.setNull();
    if (context.session->createCompositeComponentType(components, 2, composed_program.writeRef(),
                                                      diagnostics.writeRef()) != SLANG_OK ||
        !composed_program) {
        log_slang_diagnostics(source_name, diagnostics.get());
        core_log.error("Slang failed to compose shader program for %s", source_name);
        return false;
    }

    Slang::ComPtr<slang::IComponentType> linked_program;
    diagnostics.setNull();
    if (composed_program->link(linked_program.writeRef(), diagnostics.writeRef()) != SLANG_OK || !linked_program) {
        log_slang_diagnostics(source_name, diagnostics.get());
        core_log.error("Slang failed to link shader program for %s", source_name);
        return false;
    }

    Slang::ComPtr<slang::IBlob> code;
    diagnostics.setNull();
    if (linked_program->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef()) != SLANG_OK || !code) {
        log_slang_diagnostics(source_name, diagnostics.get());
        core_log.error("Slang failed to emit SPIR-V for %s", source_name);
        return false;
    }

    const size_t byte_length = code->getBufferSize();
    if (byte_length == 0 || (byte_length % sizeof(uint32_t)) != 0) {
        core_log.error("Slang produced invalid SPIR-V length for %s", source_name);
        return false;
    }

    output.resize(byte_length / sizeof(uint32_t));
    std::memcpy(output.data(), code->getBufferPointer(), byte_length);
    return true;
}

}  // namespace

sg_shader create_backend_shader(sg_shader_desc* desc, const std::string& vertex_source,
                                const std::string& fragment_source, const char* label) {
    if (!desc) return {SG_INVALID_ID};
    if (label) desc->label = label;

    if (!prepare_vulkan_bindings(desc)) return {SG_INVALID_ID};

    std::string linked_vertex_source = vertex_source;
    std::string linked_fragment_source = fragment_source;
    assign_matching_varying_locations(linked_vertex_source, linked_fragment_source);

    const std::string vulkan_vs = make_vulkan_source(*desc, linked_vertex_source, SG_SHADERSTAGE_VERTEX);
    const std::string vulkan_fs = make_vulkan_source(*desc, linked_fragment_source, SG_SHADERSTAGE_FRAGMENT);

    std::vector<uint32_t> vertex_spirv;
    std::vector<uint32_t> fragment_spirv;
    const std::string vertex_name = std::string(label ? label : "shader") + ".vert";
    const std::string fragment_name = std::string(label ? label : "shader") + ".frag";

    const bool vertex_ok = compile_spirv(SLANG_STAGE_VERTEX, vulkan_vs, vertex_name.c_str(), vertex_spirv);
    const bool fragment_ok = compile_spirv(SLANG_STAGE_FRAGMENT, vulkan_fs, fragment_name.c_str(), fragment_spirv);
    if (!vertex_ok || !fragment_ok) return {SG_INVALID_ID};

    desc->vertex_func.source = nullptr;
    desc->fragment_func.source = nullptr;
    desc->vertex_func.bytecode = {vertex_spirv.data(), vertex_spirv.size() * sizeof(uint32_t)};
    desc->fragment_func.bytecode = {fragment_spirv.data(), fragment_spirv.size() * sizeof(uint32_t)};
    desc->vertex_func.entry = "main";
    desc->fragment_func.entry = "main";

    return sg_make_shader(desc);
}
