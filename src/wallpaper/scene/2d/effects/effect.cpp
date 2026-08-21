#include "effect.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/build_config.h"
#include "core/config.h"
#include "core/context.h"
#include "core/engine_context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "effect_configuration.h"
#include "formats/wallpaper_engine/scene/scene_document.h"
#include "render/shader/shader_processor.h"
#include "sokol_app.h"

#if DEBUG_BUILD
#include "render/diagnostics/render_diagnostics.h"
#endif

namespace {
struct ShaderUniformMetadata {
    std::string name;
    std::string material;
    std::string type = "float";
    std::vector<float> default_values;
    bool has_default = false;
};

std::string normalizeUniformName(const std::string& name) {
    std::string normalized;
    size_t start = 0;
    if (name.size() > 2 && (name[0] == 'g' || name[0] == 'G' || name[0] == 'u' || name[0] == 'U') && name[1] == '_')
        start = 2;
    for (size_t i = start; i < name.size(); ++i) {
        unsigned char c = (unsigned char)name[i];
        if (std::isalnum(c)) normalized.push_back((char)std::tolower(c));
    }
    return normalized;
}

std::vector<ShaderUniformMetadata> extractShaderUniforms(const std::string& source) {
    std::vector<ShaderUniformMetadata> result;
    size_t search = 0;
    while ((search = source.find("uniform", search)) != std::string::npos) {
        if (search > 0) {
            unsigned char before = (unsigned char)source[search - 1];
            if (std::isalnum(before) || before == '_') {
                search += 7;
                continue;
            }
        }

        const size_t semicolon = source.find(';', search + 7);
        if (semicolon == std::string::npos) break;
        const std::string declaration = source.substr(search + 7, semicolon - search - 7);

        std::istringstream stream(declaration);
        std::vector<std::string> tokens;
        std::string token;
        bool is_sampler = false;
        while (stream >> token) {
            if (token.find("sampler") != std::string::npos) is_sampler = true;
            tokens.push_back(token);
        }
        if (!is_sampler && tokens.size() >= 2) {
            std::string type = tokens[0];
            std::string name = tokens.back();
            const size_t array = name.find('[');
            if (array != std::string::npos) name.erase(array);
            const size_t assign = name.find('=');
            if (assign != std::string::npos) name.erase(assign);

            std::string material;
            std::vector<float> default_values;
            bool has_default = false;

            const size_t line_end = source.find('\n', semicolon + 1);
            const size_t comment = source.find("//", semicolon + 1);
            if (comment != std::string::npos && (line_end == std::string::npos || comment < line_end)) {
                const size_t json_start = source.find('{', comment + 2);
                const size_t json_limit = line_end == std::string::npos ? source.size() : line_end;
                if (json_start != std::string::npos && json_start < json_limit && json_limit > 0) {
                    const size_t json_end = source.rfind('}', json_limit - 1);
                    if (json_end != std::string::npos && json_end >= json_start) {
                        const std::string metadata_text = source.substr(json_start, json_end - json_start + 1);
                        cJSON* metadata = cJSON_Parse(metadata_text.c_str());
                        if (metadata) {
                            cJSON* material_node = cJSON_GetObjectItemCaseSensitive(metadata, "material");
                            if (cJSON_IsString(material_node) && material_node->valuestring)
                                material = material_node->valuestring;
                            cJSON* type_node = cJSON_GetObjectItemCaseSensitive(metadata, "type");
                            if (cJSON_IsString(type_node) && type_node->valuestring) type = type_node->valuestring;
                            cJSON* def_node = cJSON_GetObjectItemCaseSensitive(metadata, "default");
                            if (def_node) {
                                if (EffectConfiguration::readFloats(def_node, default_values) &&
                                    !default_values.empty()) {
                                    has_default = true;
                                }
                            }
                            cJSON_Delete(metadata);
                        }
                    }
                }
            }

            if (!name.empty()) result.push_back({name, material, type, default_values, has_default});
        }
        search = semicolon + 1;
    }
    return result;
}

bool resolveUniformName(const std::string& authored, const std::vector<ShaderUniformMetadata>& shader_uniforms,
                        std::string& resolved) {
    std::string preferred = authored;
    auto mapped = Config::kUniformNameMap.find(authored);
    if (mapped != Config::kUniformNameMap.end()) preferred = mapped->second;

    auto find_name = [&](const std::string& requested) {
        for (const auto& candidate : shader_uniforms) {
            if (candidate.name == requested) {
                resolved = candidate.name;
                return true;
            }
        }
        return false;
    };

    if (find_name(preferred) || (preferred != authored && find_name(authored))) return true;

    auto find_material = [&](const std::string& requested, bool normalized) {
        std::string match;
        const std::string wanted = normalized ? normalizeUniformName(requested) : requested;
        for (const auto& candidate : shader_uniforms) {
            if (candidate.material.empty()) continue;
            const std::string material = normalized ? normalizeUniformName(candidate.material) : candidate.material;
            if (material != wanted) continue;
            if (!match.empty() && match != candidate.name) return false;
            match = candidate.name;
        }
        if (match.empty()) return false;
        resolved = match;
        return true;
    };

    if (find_material(authored, false) || (preferred != authored && find_material(preferred, false))) return true;
    if (find_material(authored, true) || (preferred != authored && find_material(preferred, true))) return true;

    const std::string wanted = normalizeUniformName(preferred);
    for (const auto& candidate : shader_uniforms) {
        if (normalizeUniformName(candidate.name) == wanted) {
            resolved = candidate.name;
            return true;
        }
    }
    if (preferred != authored) {
        const std::string authored_wanted = normalizeUniformName(authored);
        for (const auto& candidate : shader_uniforms) {
            if (normalizeUniformName(candidate.name) == authored_wanted) {
                resolved = candidate.name;
                return true;
            }
        }
    }

    return false;
}

void setComboDefine(std::string& combo_defines, const std::string& requested_name, int value) {
    auto replace_define = [&](const std::string& name) {
        const std::string prefix = "#define " + name + " ";
        const size_t pos = combo_defines.find(prefix);
        if (pos == std::string::npos) return false;
        size_t end = combo_defines.find('\n', pos);
        if (end == std::string::npos) end = combo_defines.size();
        combo_defines.replace(pos, end - pos, prefix + std::to_string(value));
        return true;
    };

    if (replace_define(requested_name)) return;

    std::string upper = requested_name;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    if (replace_define(upper)) return;

    combo_defines += "#define " + requested_name + " " + std::to_string(value) + "\n";
}

}  // namespace

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx) {
    cJSON* base_config = config;
    cJSON* owned_base_config = nullptr;

    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(config, "material");
    if (cJSON_IsString(mat_ref) && mat_ref->valuestring) {
        char abs_mat[1024];
        if (ctx.asset_mgr.resolvePath(mat_ref->valuestring, abs_mat, sizeof(abs_mat))) {
            char* material_json_str = read_file_to_string(abs_mat);
            if (material_json_str) {
                cJSON* mat_json = cJSON_Parse(material_json_str);
                free(material_json_str);
                if (mat_json) {
                    cJSON* passes = cJSON_GetObjectItemCaseSensitive(mat_json, "passes");
                    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) {
                        owned_base_config = cJSON_Duplicate(cJSON_GetArrayItem(passes, 0), 1);
                    } else {
                        owned_base_config = cJSON_Duplicate(mat_json, 1);
                    }
                    cJSON_Delete(mat_json);
                    if (owned_base_config) base_config = owned_base_config;
                }
            }
        }
    }

    cJSON* shader_node = cJSON_GetObjectItemCaseSensitive(base_config, "shader");
    if (cJSON_IsString(shader_node) && shader_node->valuestring) shader_name = shader_node->valuestring;

    EffectConfiguration::readUniformValues(base_config, base_uniforms);
    EffectConfiguration::readCombos(base_config, base_combos);

    EffectConfiguration::mergeObject(constant_values,
                                     cJSON_GetObjectItemCaseSensitive(base_config, "constantshadervalues"));
    EffectConfiguration::readCombos(base_config, combos);
    pass_textures.loadFromConfig(base_config, shader_name, ctx);

    if (base_config != config) {
        EffectConfiguration::readUniformValues(config, pass_uniforms);
        EffectConfiguration::readCombos(config, pass_combos);
        EffectConfiguration::mergeObject(constant_values,
                                         cJSON_GetObjectItemCaseSensitive(config, "constantshadervalues"));
        EffectConfiguration::readCombos(config, combos);
        pass_textures.applyInstanceOverrides(config, shader_name, ctx);
    }

    if (instance_config) {
        EffectConfiguration::readUniformValues(instance_config, inst_uniforms);
        EffectConfiguration::readCombos(instance_config, inst_combos);
        pass_textures.applyInstanceOverrides(instance_config, shader_name, ctx);
        EffectConfiguration::mergeObject(constant_values,
                                         cJSON_GetObjectItemCaseSensitive(instance_config, "constantshadervalues"));
        EffectConfiguration::readCombos(instance_config, combos);

        cJSON* pass_enabled = cJSON_GetObjectItemCaseSensitive(instance_config, "enabled");
        if (cJSON_IsBool(pass_enabled)) enabled = cJSON_IsTrue(pass_enabled);
    }

    EffectConfiguration::readValuesObject(constant_values, uniforms);

    if (owned_base_config) cJSON_Delete(owned_base_config);
}

ShaderPass::~ShaderPass() {
    if (constant_values) cJSON_Delete(constant_values);
}

void ShaderPass::init(EngineContext& ctx) {
    if (shader_name.empty()) {
        effect_log.warn("Skipping effect pass with no shader");
        return;
    }

    char vert_path[256], frag_path[256];
    if (shader_name.find("shaders/") == 0) {
        snprintf(vert_path, sizeof(vert_path), "%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "%s.frag", shader_name.c_str());
    } else {
        snprintf(vert_path, sizeof(vert_path), "shaders/%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "shaders/%s.frag", shader_name.c_str());
    }

    char abs_vert[1024], abs_frag[1024];
    char* vs_src = nullptr;
    char* fs_src = nullptr;

    if (ctx.asset_mgr.resolvePath(vert_path, abs_vert, sizeof(abs_vert))) {
        vs_src = read_file_to_string(abs_vert);
    } else {
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", vert_path);
        if (ctx.asset_mgr.resolvePath(extracted_path, abs_vert, sizeof(abs_vert))) {
            vs_src = read_file_to_string(abs_vert);
        }
    }

    if (ctx.asset_mgr.resolvePath(frag_path, abs_frag, sizeof(abs_frag))) {
        fs_src = read_file_to_string(abs_frag);
    } else {
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", frag_path);
        if (ctx.asset_mgr.resolvePath(extracted_path, abs_frag, sizeof(abs_frag))) {
            fs_src = read_file_to_string(abs_frag);
        }
    }

    if (!vs_src || !fs_src) {
        effect_log.warn("ShaderPass %s: missing vertex or fragment shader source", shader_name.c_str());
        if (vs_src) free(vs_src);
        if (fs_src) free(fs_src);
        return;
    }

    std::string raw_vs = vs_src;
    std::string raw_fs = fs_src;
    std::string combo_defines;

    const bool is_depth_parallax = shader_name.find("depthparallax") != std::string::npos;
    const bool is_waterwaves = shader_name.find("waterwaves") != std::string::npos;
    const bool has_mask_texture_combo = raw_fs.find("\"combo\":\"MASK\"") != std::string::npos;

    bool has_mvp = raw_vs.find("g_ModelViewProjectionMatrix") != std::string::npos;
    int vertical = combos.count("VERTICAL") ? combos.at("VERTICAL") : 0;
    is_fullscreen_quad = !render_target.empty() && (!has_mvp || vertical == 0);

    std::vector<ShaderUniformMetadata> shader_uniforms = extractShaderUniforms(raw_vs);
    std::vector<ShaderUniformMetadata> fragment_uniforms = extractShaderUniforms(raw_fs);
    shader_uniforms.insert(shader_uniforms.end(), fragment_uniforms.begin(), fragment_uniforms.end());

    std::map<std::string, std::vector<float>> resolved_uniforms;
    for (const auto& [name, values] : uniforms) {
        std::string resolved_name;
        if (!resolveUniformName(name, shader_uniforms, resolved_name)) {
            effect_log.warn(
                "ShaderPass %s: authored constant '%s' has no matching shader uniform; value will not be bound",
                shader_name.c_str(), name.c_str());
            continue;
        }

        auto existing = resolved_uniforms.find(resolved_name);
        if (existing != resolved_uniforms.end() && existing->second != values) {
            effect_log.warn("ShaderPass %s: authored constant '%s' collides on shader uniform '%s'; using latest value",
                            shader_name.c_str(), name.c_str(), resolved_name.c_str());
        }
        resolved_uniforms[resolved_name] = values;

        if (resolved_name != name) {
            std::ostringstream value_text;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) value_text << ',';
                value_text << values[i];
            }
            effect_log.debug("ShaderPass %s: mapped authored constant '%s' -> '%s' = [%s]", shader_name.c_str(),
                             name.c_str(), resolved_name.c_str(), value_text.str().c_str());
        }
    }
    uniforms = std::move(resolved_uniforms);

    std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    std::string processed_vs = ShaderSourceProcessor::processShaderSource(raw_vs, abs_vert, ctx.asset_mgr, true);
    std::string processed_fs = ShaderSourceProcessor::processShaderSource(raw_fs, abs_frag, ctx.asset_mgr, false);
    combo_defines = ShaderSourceProcessor::extractCombos(processed_fs.c_str());
    for (const auto& [name, value] : combos) setComboDefine(combo_defines, name, value);
    if (is_depth_parallax) {
        setComboDefine(combo_defines, "MASK",
                       pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID);
    } else if (is_waterwaves) {
        setComboDefine(combo_defines, "MASK",
                       !pass_textures.textures.empty() && pass_textures.textures[0].id != SG_INVALID_ID);
        setComboDefine(combo_defines, "TIMEOFFSET",
                       pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID);
    } else if (has_mask_texture_combo) {
        // Some effects declare MASK on the g_Texture1 metadata instead of using
        // a standalone [COMBO] annotation. Respect that declaration so an
        // unmasked fallback does not turn a selective effect into a full-frame one.
        setComboDefine(combo_defines, "MASK",
                       !pass_textures.textures.empty() && pass_textures.textures[0].id != SG_INVALID_ID);
    }

    if (is_depth_parallax) {
        const std::string mask_sample = "texSample2D(g_Texture2, v_TexCoordMask.xy).r";
        const size_t mask_sample_pos = processed_fs.find(mask_sample);
        if (mask_sample_pos != std::string::npos) {
            processed_fs.replace(mask_sample_pos, mask_sample.size(), "lwe_srgb_to_linear(" + mask_sample + ")");
            processed_fs =
                "float lwe_srgb_to_linear(float c) {\n"
                "    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);\n"
                "}\n" +
                processed_fs;
        }
    }

    std::string full_vs = prefix + combo_defines + processed_vs;
    std::string full_fs = prefix + combo_defines + processed_fs;

    stored_vs_source = full_vs;
    stored_fs_source = full_fs;

#if DEBUG_BUILD
    full_fs = ShaderCompiler::applyDebugMode(full_fs, debug_view_mode);
    full_fs = ShaderCompiler::applyDebugStep(shader_name, full_fs, debug_step);
#endif

    texture_labels = ShaderSourceProcessor::extractTextureLabels(fs_src);

    int texture_count = (int)pass_textures.textures.size();
    for (const auto& [slot, binding] : render_texture_bindings) {
        (void)binding;
        // Runtime render targets are bound by ImageLayer rather than listed in
        // a material's texture array. They still need shader sampler slots.
        if (slot > 0) texture_count = std::max(texture_count, slot);
    }
    compiled = ShaderCompiler::compile(shader_name, full_vs, full_fs, uniforms, texture_count);

    free(vs_src);
    free(fs_src);

    pass_textures.buildCachedViews();

#if DEBUG_BUILD
    PassUniformProvenance prov;
    prov.effect_file = effect_file;
    prov.pass_index = pass_index;
    prov.shader_name = shader_name;

    for (const auto& meta : shader_uniforms) {
        UniformProvenanceEntry entry;
        entry.shader_name = meta.name;
        entry.authored_name = meta.material;
        entry.resolved_name = meta.name;
        entry.type = meta.type;

        UniformResolutionStep step_def;
        step_def.source = ProvenanceSource::ShaderMetadataDefault;
        step_def.source_name = "shader_metadata_default";
        step_def.present = meta.has_default;
        step_def.values = meta.default_values;
        step_def.applied = false;
        entry.resolution.push_back(step_def);

        bool found_base = false;
        std::vector<float> base_val;
        for (const auto& [b_name, b_val] : base_uniforms) {
            std::string res;
            if (resolveUniformName(b_name, {meta}, res) && res == meta.name) {
                found_base = true;
                base_val = b_val;
                break;
            }
        }
        UniformResolutionStep step_base;
        step_base.source = ProvenanceSource::MaterialConstant;
        step_base.source_name = "material_constant";
        step_base.present = found_base;
        step_base.values = base_val;
        entry.resolution.push_back(step_base);

        bool found_pass = false;
        std::vector<float> pass_val;
        for (const auto& [p_name, p_val] : pass_uniforms) {
            std::string res;
            if (resolveUniformName(p_name, {meta}, res) && res == meta.name) {
                found_pass = true;
                pass_val = p_val;
                break;
            }
        }
        UniformResolutionStep step_pass;
        step_pass.source = ProvenanceSource::EffectPassOverride;
        step_pass.source_name = "effect_pass_override";
        step_pass.present = found_pass;
        step_pass.values = pass_val;
        entry.resolution.push_back(step_pass);

        bool found_inst = false;
        std::vector<float> inst_val;
        for (const auto& [i_name, i_val] : inst_uniforms) {
            std::string res;
            if (resolveUniformName(i_name, {meta}, res) && res == meta.name) {
                found_inst = true;
                inst_val = i_val;
                break;
            }
        }
        UniformResolutionStep step_inst;
        step_inst.source = ProvenanceSource::InstanceOverride;
        step_inst.source_name = "instance_override";
        step_inst.present = found_inst;
        step_inst.values = inst_val;
        entry.resolution.push_back(step_inst);

        auto final_it = uniforms.find(meta.name);
        if (final_it != uniforms.end()) {
            entry.final_value = final_it->second;
            if (found_inst) {
                entry.final_source = ProvenanceSource::InstanceOverride;
                entry.resolution.back().applied = true;
            } else if (found_pass) {
                entry.final_source = ProvenanceSource::EffectPassOverride;
                entry.resolution[2].applied = true;
            } else if (found_base) {
                entry.final_source = ProvenanceSource::MaterialConstant;
                entry.resolution[1].applied = true;
            } else {
                entry.final_source = ProvenanceSource::RuntimeBuiltin;
            }
        } else {
            if (meta.has_default) {
                entry.final_source = ProvenanceSource::ShaderMetadataDefault;
                entry.final_value = meta.default_values;
                entry.resolution[0].note = "metadata default exists but not bound by runtime";
            } else {
                entry.final_source = ProvenanceSource::Unresolved;
            }
        }

        prov.uniforms[meta.name] = entry;
    }

    for (const auto& [c_name, c_val] : combos) {
        ComboProvenanceEntry c_entry;
        c_entry.name = c_name;
        c_entry.final_value = c_val;

        if (base_combos.count(c_name)) {
            ComboResolutionStep s;
            s.source = ProvenanceSource::MaterialConstant;
            s.source_name = "material_constant";
            s.present = true;
            s.value = base_combos.at(c_name);
            c_entry.resolution.push_back(s);
        }
        if (pass_combos.count(c_name)) {
            ComboResolutionStep s;
            s.source = ProvenanceSource::EffectPassOverride;
            s.source_name = "effect_pass_override";
            s.present = true;
            s.value = pass_combos.at(c_name);
            c_entry.resolution.push_back(s);
        }
        if (inst_combos.count(c_name)) {
            ComboResolutionStep s;
            s.source = ProvenanceSource::InstanceOverride;
            s.source_name = "instance_override";
            s.present = true;
            s.value = inst_combos.at(c_name);
            c_entry.resolution.push_back(s);
        }

        if (inst_combos.count(c_name)) {
            c_entry.final_source = ProvenanceSource::InstanceOverride;
            if (!c_entry.resolution.empty()) c_entry.resolution.back().applied = true;
        } else if (pass_combos.count(c_name)) {
            c_entry.final_source = ProvenanceSource::EffectPassOverride;
            if (!c_entry.resolution.empty()) c_entry.resolution.back().applied = true;
        } else if (base_combos.count(c_name)) {
            c_entry.final_source = ProvenanceSource::MaterialConstant;
            if (!c_entry.resolution.empty()) c_entry.resolution.back().applied = true;
        } else {
            c_entry.final_source = ProvenanceSource::RuntimeInferred;
        }

        prov.combos[c_name] = c_entry;
    }

    ShaderDump dump;
    dump.effect_index = effect_index;
    dump.pass_index = pass_index;
    dump.shader_name = shader_name;
    dump.effect_file = effect_file;
    dump.original_vs = raw_vs;
    dump.original_fs = raw_fs;
    dump.processed_vs = processed_vs;
    dump.processed_fs = processed_fs;
    dump.final_vs = full_vs;
    dump.final_fs = full_fs;
    dump.combos = combos;
    dump.uniforms = uniforms;

    RenderDiagnostics::instance().registerShaderDump(dump);
    RenderDiagnostics::instance().registerUniformProvenance(prov);
#endif

    if (compiled.pipeline.id == SG_INVALID_ID) {
        effect_log.warn("ShaderPass %s: effect shader could not be compiled; pass will be skipped",
                        shader_name.c_str());
        return;
    }

    if (is_depth_parallax) {
        if (pass_textures.textures.empty() || pass_textures.textures[0].id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture1 (depth) missing, using Wallpaper Engine black fallback",
                            shader_name.c_str());
        }
        if (pass_textures.textures.size() < 2 || pass_textures.textures[1].id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture2 (mask) missing, using full white fallback", shader_name.c_str());
        }
    } else if (is_waterwaves) {
        if (pass_textures.textures.empty() || pass_textures.textures[0].id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture1 (mask) missing, using full white fallback", shader_name.c_str());
        }
    }
}

void ShaderPass::apply(EngineContext& ctx) {
    (void)ctx;
    if (!enabled || compiled.pipeline.id == SG_INVALID_ID) return;
}

void ShaderPass::applyUniforms() {
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        (void)name;
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        alignas(16) float data[4] = {0, 0, 0, 0};
        for (int i = 0; i < (int)vals.size() && i < 4; i++) data[i] = vals[i];
        sg_range range = SG_RANGE(data);
        sg_apply_uniforms(u_idx, &range);
        u_idx++;
    }
}

bool ShaderPass::resolveDepth(const char* source_tex_path, EngineContext& ctx) {
    const bool first_attempt = !pass_textures.depth_attempted;
    const bool resolved = pass_textures.resolveDepth(source_tex_path, shader_name, ctx);

    if (resolved || !first_attempt || shader_name.find("depthparallax") == std::string::npos) return resolved;

    const bool has_depth = !pass_textures.textures.empty() && pass_textures.textures[0].id != SG_INVALID_ID;
    const bool has_mask = pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID;
    if (!has_depth && has_mask) {
        auto center = uniforms.find("g_Center");
        if (center != uniforms.end() && !center->second.empty()) {
            center->second[0] = 0.0f;
            effect_log.info(
                "ShaderPass %s: depth map unavailable; disabling unmasked focal-plane shift while preserving masked "
                "parallax",
                shader_name.c_str());
        }
    }

    return resolved;
}

#if DEBUG_BUILD
void ShaderPass::rebuildWithDebugMode(int mode, EngineContext& ctx) {
    debug_view_mode = mode;
    compiled = {};
    init(ctx);
}
#endif

Effect::Effect(cJSON* config, EngineContext& ctx) {
    std::map<std::string, float> target_scales;
    cJSON* fbos_node = cJSON_GetObjectItemCaseSensitive(config, "fbos");
    if (cJSON_IsArray(fbos_node)) {
        cJSON* fbo;
        cJSON_ArrayForEach(fbo, fbos_node) {
            cJSON* name = cJSON_GetObjectItemCaseSensitive(fbo, "name");
            cJSON* scale = cJSON_GetObjectItemCaseSensitive(fbo, "scale");
            if (cJSON_IsString(name) && name->valuestring && cJSON_IsNumber(scale) && scale->valuedouble > 0.0)
                target_scales[name->valuestring] = (float)scale->valuedouble;
        }
    }
    cJSON* passes_node = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(passes_node)) {
        cJSON* pass_json;
        cJSON_ArrayForEach(pass_json, passes_node) {
            auto* pass = new ShaderPass(pass_json, nullptr, ctx);
            cJSON* target = cJSON_GetObjectItemCaseSensitive(pass_json, "target");
            if (cJSON_IsString(target) && target->valuestring) {
                pass->render_target = target->valuestring;
                auto scale = target_scales.find(pass->render_target);
                if (scale != target_scales.end()) pass->render_scale = scale->second;
            }
            cJSON* bind = cJSON_GetObjectItemCaseSensitive(pass_json, "bind");
            if (cJSON_IsArray(bind)) {
                cJSON* entry;
                cJSON_ArrayForEach(entry, bind) {
                    cJSON* slot = cJSON_GetObjectItemCaseSensitive(entry, "index");
                    cJSON* source = cJSON_GetObjectItemCaseSensitive(entry, "name");
                    if (cJSON_IsNumber(slot) && cJSON_IsString(source) && source->valuestring)
                        pass->render_texture_bindings[slot->valueint] = source->valuestring;
                }
            }
            passes.push_back(pass);
        }
    }
}

Effect::~Effect() {
    for (auto p : passes) delete p;
    passes.clear();
}

Effect* Effect::load(const char* rel_path, cJSON* instance_config, EngineContext& ctx) {
    if (!rel_path || !rel_path[0]) return nullptr;

    char abs_path[1024];
    if (!ctx.asset_mgr.resolvePath(rel_path, abs_path, sizeof(abs_path))) {
        effect_log.warn("Effect definition not found: %s", rel_path);
        return nullptr;
    }

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* config = cJSON_Parse(json_str);
    free(json_str);
    if (!config) {
        effect_log.warn("Failed to parse effect definition: %s", rel_path);
        return nullptr;
    }

    Effect* effect = new Effect(config, ctx);
    effect->file_path = rel_path;

    cJSON* inst_passes = cJSON_GetObjectItemCaseSensitive(instance_config, "passes");
    cJSON* config_passes = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(inst_passes) && cJSON_IsArray(config_passes)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_passes); i++) {
            if (i >= (int)effect->passes.size() || i >= cJSON_GetArraySize(config_passes)) break;
            cJSON* pass_config = cJSON_GetArrayItem(config_passes, i);
            cJSON* inst_pass_config = cJSON_GetArrayItem(inst_passes, i);
            ShaderPass* old_pass = effect->passes[i];
            auto* new_pass = new ShaderPass(pass_config, inst_pass_config, ctx);
            new_pass->render_target = old_pass->render_target;
            new_pass->render_scale = old_pass->render_scale;
            new_pass->render_texture_bindings = old_pass->render_texture_bindings;
            delete old_pass;
            effect->passes[i] = new_pass;
        }
    }

    cJSON* vis = cJSON_GetObjectItemCaseSensitive(instance_config, "visible");
    if (cJSON_IsBool(vis))
        effect->visible = cJSON_IsTrue(vis);
    else if (cJSON_IsObject(vis)) {
        cJSON* val = cJSON_GetObjectItemCaseSensitive(vis, "value");
        if (cJSON_IsBool(val)) effect->visible = cJSON_IsTrue(val);
    }

    for (size_t i = 0; i < effect->passes.size(); ++i) {
        effect->passes[i]->pass_index = (int)i;
        effect->passes[i]->effect_file = effect->file_path;
    }

    effect->init(ctx);
    cJSON_Delete(config);

    if (effect->passes.empty()) {
        effect_log.warn("Effect %s has no render passes", rel_path);
    } else {
        effect_log.info("Loaded generic Wallpaper Engine effect: %s (%zu pass%s)", rel_path, effect->passes.size(),
                        effect->passes.size() == 1 ? "" : "es");
    }
    return effect;
}

Effect* Effect::loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx) {
    cJSON* inst_json = nullptr;
    if (!doc.instance_config_json.empty()) inst_json = cJSON_Parse(doc.instance_config_json.c_str());

    Effect* eff = load(doc.file.c_str(), inst_json, ctx);
    if (eff) eff->visible = doc.visible;
    if (inst_json) cJSON_Delete(inst_json);
    return eff;
}

void Effect::init(EngineContext& ctx) {
    for (auto p : passes) p->init(ctx);
}

void Effect::apply(EngineContext& ctx) {
    for (auto p : passes) p->apply(ctx);
}
