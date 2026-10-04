#include <cctype>
#include <cstdlib>
#include <sstream>

#include "pass_loader.h"
#include "shader_pass.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/phase_timer.h"
#include "shared/core/task_pool.h"
#include "shared/core/utils.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/shader/shader_processor.h"
#include "wallpaper/2d/effects/effect_parser.h"

namespace {

bool findResolvedValue(const std::map<std::string, std::vector<float>>& values, const ShaderUniformConfig& meta,
                       std::vector<float>& found_value) {
    for (const auto& [name, value] : values) {
        std::string resolved;
        if (EffectParser::resolveUniformName(name, {meta}, resolved) && resolved == meta.name) {
            found_value = value;
            return true;
        }
    }
    return false;
}

UniformResolutionStep makeStep(ProvenanceSource source, const char* source_name, bool present,
                               std::vector<float> values = {}) {
    UniformResolutionStep step;
    step.source = source;
    step.source_name = source_name;
    step.present = present;
    step.values = std::move(values);
    step.applied = false;
    return step;
}

void addComboStep(ComboProvenanceEntry& entry, const std::map<std::string, int>& values, ProvenanceSource source,
                  const char* source_name) {
    const auto found = values.find(entry.name);
    if (found == values.end()) return;
    ComboResolutionStep step;
    step.source = source;
    step.source_name = source_name;
    step.present = true;
    step.value = found->second;
    entry.resolution.push_back(step);
}

}  // namespace

void ShaderPass::warnAboutMissingTextures() const {
    const auto unbound = [&](size_t slot) {
        return pass_textures.textures.size() <= slot || pass_textures.textures[slot].id == SG_INVALID_ID;
    };
    if (shader_name.find("depthparallax") != std::string::npos) {
        if (unbound(0)) {
            effect_log.warn("ShaderPass %s: g_Texture1 (depth) missing, using Wallpaper Engine black fallback",
                            shader_name.c_str());
        }
        if (unbound(1)) {
            effect_log.warn("ShaderPass %s: g_Texture2 (mask) missing, using full white fallback", shader_name.c_str());
        }
    } else if (shader_name.find("waterwaves") != std::string::npos && unbound(0)) {
        effect_log.warn("ShaderPass %s: g_Texture1 (mask) missing, using full white fallback", shader_name.c_str());
    }
}

void ShaderPass::registerDiagnostics(const ShaderSourceSet& sources,
                                     const std::vector<ShaderUniformConfig>& shader_uniforms) const {
    PassUniformProvenance prov;
    prov.effect_file = effect_file;
    prov.pass_index = pass_index;
    prov.shader_name = shader_name;

    for (const ShaderUniformConfig& meta : shader_uniforms) {
        UniformProvenanceEntry entry;
        entry.shader_name = meta.name;
        entry.authored_name = meta.material_name;
        entry.resolved_name = meta.name;
        entry.type = meta.type;

        std::vector<float> base_value, pass_value, instance_value;
        const bool found_base = findResolvedValue(base_uniforms, meta, base_value);
        const bool found_pass = findResolvedValue(pass_uniforms, meta, pass_value);
        const bool found_instance = findResolvedValue(inst_uniforms, meta, instance_value);

        UniformResolutionStep default_step = makeStep(ProvenanceSource::ShaderMetadataDefault,
                                                      "shader_metadata_default", meta.has_default, meta.default_values);
        default_step.applied = meta.has_default && !found_base && !found_pass && !found_instance;
        UniformResolutionStep base_step =
            makeStep(ProvenanceSource::MaterialConstant, "material_constant", found_base, base_value);
        UniformResolutionStep pass_step =
            makeStep(ProvenanceSource::EffectPassOverride, "effect_pass_override", found_pass, pass_value);
        UniformResolutionStep instance_step =
            makeStep(ProvenanceSource::InstanceOverride, "instance_override", found_instance, instance_value);

        const auto final_value = uniforms.find(meta.name);
        if (final_value == uniforms.end()) {
            entry.final_source = ProvenanceSource::Unresolved;
        } else {
            entry.final_value = final_value->second;
            if (found_instance) {
                entry.final_source = ProvenanceSource::InstanceOverride;
                instance_step.applied = true;
            } else if (found_pass) {
                entry.final_source = ProvenanceSource::EffectPassOverride;
                pass_step.applied = true;
            } else if (found_base) {
                entry.final_source = ProvenanceSource::MaterialConstant;
                base_step.applied = true;
            } else {
                entry.final_source =
                    meta.has_default ? ProvenanceSource::ShaderMetadataDefault : ProvenanceSource::RuntimeBuiltin;
            }
        }
        entry.resolution = {default_step, base_step, pass_step, instance_step};
        prov.uniforms[meta.name] = entry;
    }

    for (const auto& [combo_name, combo_value] : combos) {
        ComboProvenanceEntry entry;
        entry.name = combo_name;
        entry.final_value = combo_value;
        addComboStep(entry, base_combos, ProvenanceSource::MaterialConstant, "material_constant");
        addComboStep(entry, pass_combos, ProvenanceSource::EffectPassOverride, "effect_pass_override");
        addComboStep(entry, inst_combos, ProvenanceSource::InstanceOverride, "instance_override");

        if (inst_combos.count(combo_name))
            entry.final_source = ProvenanceSource::InstanceOverride;
        else if (pass_combos.count(combo_name))
            entry.final_source = ProvenanceSource::EffectPassOverride;
        else if (base_combos.count(combo_name))
            entry.final_source = ProvenanceSource::MaterialConstant;
        else
            entry.final_source = ProvenanceSource::RuntimeInferred;
        if (!entry.resolution.empty() && entry.final_source != ProvenanceSource::RuntimeInferred)
            entry.resolution.back().applied = true;
        prov.combos[combo_name] = entry;
    }

    ShaderDump dump;
    dump.effect_index = effect_index;
    dump.pass_index = pass_index;
    dump.shader_name = shader_name;
    dump.effect_file = effect_file;
    dump.original_vs = sources.raw_vs;
    dump.original_fs = sources.raw_fs;
    dump.processed_vs = sources.processed_vs;
    dump.processed_fs = sources.processed_fs;
    dump.final_vs = sources.full_vs;
    dump.final_fs = sources.full_fs;
    dump.combos = combos;
    dump.uniforms = uniforms;

    renderObserver().registerShaderDump(dump);
    renderObserver().registerUniformProvenance(prov);
}
