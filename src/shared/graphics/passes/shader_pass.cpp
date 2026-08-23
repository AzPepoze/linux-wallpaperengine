#include "shader_pass.h"

#include <cstdlib>
#include <sstream>

#include "pass_loader.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "shared/graphics/shader/shader_processor.h"
#include "wallpaper/2d/effects/effect_parser.h"

namespace {

void setComboDefine(std::string& combo_defines, const std::string& name, int value) {
    std::string requested_name = name;
    if (requested_name.rfind("COMBO_", 0) == 0) requested_name = requested_name.substr(6);
    if (requested_name.rfind("combo_", 0) == 0) requested_name = requested_name.substr(6);

    for (const std::string& prefix : {"#define " + requested_name + " ", "#define COMBO_" + requested_name + " ",
                                      "#define combo_" + requested_name + " "}) {
        size_t pos = combo_defines.find(prefix);
        if (pos != std::string::npos) {
            size_t end = combo_defines.find('\n', pos);
            combo_defines.replace(pos, end == std::string::npos ? end : end - pos + 1,
                                  "#define " + requested_name + " " + std::to_string(value) + "\n");
            return;
        }
    }

    combo_defines += "#define " + requested_name + " " + std::to_string(value) + "\n";
}

}  // namespace

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx) {
    cJSON* base_config = config;
    cJSON* owned_base_config = nullptr;
    const cJSON* material_reference = cJSON_GetObjectItemCaseSensitive(config, "material");
    if (cJSON_IsString(material_reference) && material_reference->valuestring) {
        char material_path[1024];
        if (ctx.asset_mgr.resolvePath(material_reference->valuestring, material_path, sizeof(material_path))) {
            char* material_text = read_file_to_string(material_path);
            if (material_text) {
                cJSON* material_document = cJSON_Parse(material_text);
                free(material_text);
                if (material_document) {
                    const cJSON* passes = cJSON_GetObjectItemCaseSensitive(material_document, "passes");
                    owned_base_config = cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0
                                            ? cJSON_Duplicate(cJSON_GetArrayItem(passes, 0), 1)
                                            : cJSON_Duplicate(material_document, 1);
                    cJSON_Delete(material_document);
                    if (owned_base_config) base_config = owned_base_config;
                }
            }
        }
    }
    const EffectPassConfig parsed_config = EffectParser::buildPassConfig(base_config, config, instance_config);
    shader_name = parsed_config.shader_path;
    base_uniforms = parsed_config.material_uniform_values;
    pass_uniforms = parsed_config.pass_uniform_values;
    inst_uniforms = parsed_config.instance_uniform_values;
    base_combos = parsed_config.material_combos;
    pass_combos = parsed_config.pass_combos;
    inst_combos = parsed_config.instance_combos;
    uniforms = parsed_config.uniform_values;
    combos = parsed_config.combos;
    enabled = parsed_config.enabled;
    pass_textures.loadFromConfig(base_config, shader_name, ctx);
    if (base_config != config) pass_textures.applyInstanceOverrides(config, shader_name, ctx);
    if (instance_config) pass_textures.applyInstanceOverrides(instance_config, shader_name, ctx);
    if (owned_base_config) cJSON_Delete(owned_base_config);
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

    std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    std::string processed_vs = ShaderSourceProcessor::processShaderSource(raw_vs, abs_vert, ctx.asset_mgr, true);
    std::string processed_fs = ShaderSourceProcessor::processShaderSource(raw_fs, abs_frag, ctx.asset_mgr, false);

    // Shared includes can declare material uniforms, so inspect the expanded sources.
    std::vector<ShaderUniformConfig> shader_uniforms = EffectParser::extractShaderUniforms(processed_vs);
    std::vector<ShaderUniformConfig> fragment_uniforms = EffectParser::extractShaderUniforms(processed_fs);
    shader_uniforms.insert(shader_uniforms.end(), fragment_uniforms.begin(), fragment_uniforms.end());

    std::map<std::string, std::vector<float>> resolved_uniforms;
    for (const auto& [name, values] : uniforms) {
        std::string resolved_name;
        if (!EffectParser::resolveUniformName(name, shader_uniforms, resolved_name)) {
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

    // Shader metadata supplies defaults for material constants that are omitted.
    for (const ShaderUniformConfig& uniform : shader_uniforms) {
        if (uniform.has_default && resolved_uniforms.count(uniform.name) == 0)
            resolved_uniforms[uniform.name] = uniform.default_values;
    }
    uniforms = std::move(resolved_uniforms);

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
        entry.authored_name = meta.material_name;
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
            if (EffectParser::resolveUniformName(b_name, {meta}, res) && res == meta.name) {
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
            if (EffectParser::resolveUniformName(p_name, {meta}, res) && res == meta.name) {
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
            if (EffectParser::resolveUniformName(i_name, {meta}, res) && res == meta.name) {
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

        entry.resolution[0].applied = meta.has_default && !found_base && !found_pass && !found_inst;

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
                entry.final_source =
                    meta.has_default ? ProvenanceSource::ShaderMetadataDefault : ProvenanceSource::RuntimeBuiltin;
            }
        } else {
            entry.final_source = ProvenanceSource::Unresolved;
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

bool ShaderPass::resolveDepth(const char* source_texture_path, EngineContext& ctx) {
    const bool first_attempt = !pass_textures.depth_attempted;
    const bool resolved = pass_textures.resolveDepth(source_texture_path, shader_name, ctx);
    if (resolved || !first_attempt || shader_name.find("depthparallax") == std::string::npos) return resolved;

    const bool has_depth = !pass_textures.textures.empty() && pass_textures.textures[0].id != SG_INVALID_ID;
    const bool has_mask = pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID;
    if (!has_depth && has_mask) {
        auto center = uniforms.find("g_Center");
        if (center != uniforms.end() && !center->second.empty()) {
            center->second[0] = 0.0f;
            effect_log.info(
                "ShaderPass %s: depth map unavailable; disabling unmasked focal-plane shift while preserving "
                "masked parallax",
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
