#include "shader_pass.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "pass_loader.h"
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

// Returns the texture slot (g_TextureN -> N) of the sampler marked with a
// `"combo":"MASK"` annotation, or -1 when the shader has no such sampler.
int findMaskTextureIndex(const std::string& source) {
    const size_t pos = source.find("\"combo\":\"MASK\"");
    if (pos == std::string::npos) return -1;
    size_t line_start = source.rfind('\n', pos);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    const std::string line = source.substr(line_start, pos - line_start);
    const size_t tex = line.rfind("g_Texture");
    if (tex == std::string::npos) return -1;
    return std::atoi(line.c_str() + tex + 9);
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
    animated_uniforms = parsed_config.animated_uniforms;
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

namespace {
bool readShaderStage(EngineContext& ctx, const char* relative_path, char* absolute_path, size_t capacity,
                     std::string& source) {
    if (!ctx.asset_mgr.resolvePath(relative_path, absolute_path, (int)capacity)) {
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", relative_path);
        if (!ctx.asset_mgr.resolvePath(extracted_path, absolute_path, (int)capacity)) return false;
    }
    char* text = read_file_to_string(absolute_path);
    if (!text) return false;
    source = text;
    free(text);
    return true;
}

// Highest N among the `uniform sampler2D g_TextureN` declarations. The material may supply fewer textures than the
// shader reads (a bloom pass gets its second input at draw time), and every sampler the shader uses needs a binding.
int highestDeclaredTextureSlot(const std::string& source) {
    static const std::string kSampler = "sampler2D";
    static const std::string kName = "g_Texture";
    int highest = 0;
    size_t pos = 0;
    while ((pos = source.find(kSampler, pos)) != std::string::npos) {
        pos += kSampler.size();
        if (pos >= source.size() || !std::isspace((unsigned char)source[pos])) continue;
        while (pos < source.size() && std::isspace((unsigned char)source[pos])) ++pos;
        if (source.compare(pos, kName.size(), kName) != 0) continue;
        pos += kName.size();
        int slot = 0;
        bool has_digits = false;
        while (pos < source.size() && std::isdigit((unsigned char)source[pos])) {
            slot = slot * 10 + (source[pos++] - '0');
            has_digits = true;
        }
        if (has_digits) highest = std::max(highest, slot);
    }
    return highest;
}

// The depth-parallax mask is authored in sRGB but sampled as linear data here.
void convertDepthParallaxMaskToLinear(std::string& fragment_source) {
    const std::string mask_sample = "texSample2D(g_Texture2, v_TexCoordMask.xy).r";
    const size_t mask_sample_pos = fragment_source.find(mask_sample);
    if (mask_sample_pos == std::string::npos) return;
    fragment_source.replace(mask_sample_pos, mask_sample.size(), "lwe_srgb_to_linear(" + mask_sample + ")");
    fragment_source =
        "float lwe_srgb_to_linear(float c) {\n"
        "    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);\n"
        "}\n" +
        fragment_source;
}

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

void ShaderPass::resolveUniforms(const std::vector<ShaderUniformConfig>& shader_uniforms) {
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

    resolved_animations.clear();
    for (const auto& [name, curve] : animated_uniforms) {
        std::string resolved_name;
        if (!EffectParser::resolveUniformName(name, shader_uniforms, resolved_name)) continue;
        resolved_animations[resolved_name] = curve;
        effect_log.debug("ShaderPass %s: animated constant '%s' -> '%s' (%zu keys, fps=%.0f, length=%.0f, mode=%s)",
                         shader_name.c_str(), name.c_str(), resolved_name.c_str(), curve.keys.size(), curve.fps,
                         curve.length, curve.mode.c_str());
    }
}

std::string ShaderPass::buildComboDefines(const ShaderSourceSet& sources) const {
    std::string combo_defines =
        ShaderSourceProcessor::extractCombos((sources.processed_vs + "\n" + sources.processed_fs).c_str());
    for (const auto& [name, value] : combos) setComboDefine(combo_defines, name, value);

    const auto bound = [&](size_t slot) {
        return pass_textures.textures.size() > slot && pass_textures.textures[slot].id != SG_INVALID_ID;
    };
    if (shader_name.find("depthparallax") != std::string::npos) {
        setComboDefine(combo_defines, "MASK", bound(1));
    } else if (shader_name.find("waterwaves") != std::string::npos) {
        setComboDefine(combo_defines, "MASK", bound(0));
        setComboDefine(combo_defines, "TIMEOFFSET", bound(1));
    } else if (sources.raw_fs.find("\"combo\":\"MASK\"") != std::string::npos) {
        const int mask_index = findMaskTextureIndex(sources.raw_fs);
        const bool has_mask = bound(mask_index <= 0 ? 0 : (size_t)(mask_index - 1));
        effect_log.debug("ShaderPass %s: MASK combo -> %d (mask sampler g_Texture%d, %zu textures bound)",
                         shader_name.c_str(), has_mask, mask_index, pass_textures.textures.size());
        setComboDefine(combo_defines, "MASK", has_mask);
    }
    return combo_defines;
}

void ShaderPass::init(EngineContext& ctx) {
    if (prepare(ctx, false)) finish(ctx);
}

void ShaderPass::initAsync(EngineContext& ctx) {
    pending_ = TaskPool::instance().enqueue([this, &ctx] { return prepare(ctx, true); }).share();
}

void ShaderPass::completeInit(EngineContext& ctx) {
    if (!pending_.valid()) return;
    const bool prepared = pending_.get();
    pending_ = {};
    if (prepared) finish(ctx);
}

// Everything up to the compile: it reads files and processes text, never touches the GPU, so it can run on a worker.
// With `warm_cache` the SPIR-V is generated too, so finish() only has to create GPU objects.
bool ShaderPass::prepare(EngineContext& ctx, bool warm_cache) {
    if (shader_name.empty()) {
        effect_log.warn("Skipping effect pass with no shader");
        return false;
    }

    char vert_path[256], frag_path[256];
    if (shader_name.find("shaders/") == 0) {
        snprintf(vert_path, sizeof(vert_path), "%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "%s.frag", shader_name.c_str());
    } else {
        snprintf(vert_path, sizeof(vert_path), "shaders/%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "shaders/%s.frag", shader_name.c_str());
    }

    auto prepared = std::make_shared<PreparedShader>();
    ShaderSourceSet& sources = prepared->sources;
    char abs_vert[1024], abs_frag[1024];
    const bool has_vertex = readShaderStage(ctx, vert_path, abs_vert, sizeof(abs_vert), sources.raw_vs);
    const bool has_fragment = readShaderStage(ctx, frag_path, abs_frag, sizeof(abs_frag), sources.raw_fs);
    if (!has_vertex || !has_fragment) {
        effect_log.warn("ShaderPass %s: missing vertex or fragment shader source", shader_name.c_str());
        return false;
    }

    const bool has_mvp = sources.raw_vs.find("g_ModelViewProjectionMatrix") != std::string::npos;
    const int vertical = combos.count("VERTICAL") ? combos.at("VERTICAL") : 0;
    is_fullscreen_quad = !render_target.empty() && (!has_mvp || vertical == 0);

    const std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    sources.processed_vs = ShaderSourceProcessor::processShaderSource(sources.raw_vs, abs_vert, ctx.asset_mgr, true);
    sources.processed_fs = ShaderSourceProcessor::processShaderSource(sources.raw_fs, abs_frag, ctx.asset_mgr, false);

    // Shared includes can declare material uniforms, so inspect the expanded sources.
    std::vector<ShaderUniformConfig>& shader_uniforms = prepared->shader_uniforms;
    shader_uniforms = EffectParser::extractShaderUniforms(sources.processed_vs);
    std::vector<ShaderUniformConfig> fragment_uniforms = EffectParser::extractShaderUniforms(sources.processed_fs);
    shader_uniforms.insert(shader_uniforms.end(), fragment_uniforms.begin(), fragment_uniforms.end());
    resolveUniforms(shader_uniforms);

    const std::string combo_defines = buildComboDefines(sources);
    if (shader_name.find("depthparallax") != std::string::npos) convertDepthParallaxMaskToLinear(sources.processed_fs);

    sources.full_vs = prefix + combo_defines + sources.processed_vs;
    sources.full_fs = prefix + combo_defines + sources.processed_fs;
    stored_vs_source = sources.full_vs;
    stored_fs_source = sources.full_fs;
    sources.full_fs =
        renderObserver().overrideFragmentSource(shader_name, sources.full_fs, debug_view_mode, debug_step);

    texture_labels = ShaderSourceProcessor::extractTextureLabels(sources.raw_fs.c_str());

    int texture_count = (int)pass_textures.textures.size();
    for (const auto& [slot, binding] : render_texture_bindings) {
        (void)binding;
        if (slot > 0) texture_count = std::max(texture_count, slot);
    }
    texture_count = std::max(texture_count, std::min(11, std::max(highestDeclaredTextureSlot(sources.processed_vs),
                                                                  highestDeclaredTextureSlot(sources.processed_fs))));
    prepared->texture_count = texture_count;
    if (warm_cache) {
        ShaderCompiler::prewarm(shader_name, sources.full_vs, sources.full_fs, uniforms, texture_count);
    }
    prepared_ = std::move(prepared);
    return true;
}

void ShaderPass::finish(EngineContext& ctx) {
    (void)ctx;
    const std::shared_ptr<PreparedShader> prepared = std::move(prepared_);
    if (!prepared) return;
    const ShaderSourceSet& sources = prepared->sources;
    compiled =
        ShaderCompiler::compile(shader_name, sources.full_vs, sources.full_fs, uniforms, prepared->texture_count);

    pass_textures.buildCachedViews();
    if (renderObserver().isCollectingShaderInfo()) registerDiagnostics(sources, prepared->shader_uniforms);

    if (compiled.pipeline.id == SG_INVALID_ID) {
        effect_log.warn("ShaderPass %s: effect shader could not be compiled; pass will be skipped",
                        shader_name.c_str());
        return;
    }
    warnAboutMissingTextures();
}

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

void ShaderPass::updateAnimatedUniforms(float time) {
    for (const auto& [name, curve] : resolved_animations) {
        const float value = evaluateCurve(curve.keys, curve.fps, curve.length, curve.mode, time);
        auto it = uniforms.find(name);
        if (it == uniforms.end() || it->second.empty()) {
            uniforms[name] = {value};
        } else if (it->second.size() == 1) {
            it->second[0] = value;
        }
    }
}

void ShaderPass::applyAudioSpectrumBlocks() {
    if (compiled.audio_spectrum_blocks.empty()) return;
    const AudioEngine::Spectrum& spectrum = AudioEngine::instance().spectrum();
    for (const CompiledAudioSpectrumBlock& block : compiled.audio_spectrum_blocks) {
        if (block.slot < 0 || block.members.empty() || block.size_bytes == 0) continue;
        std::vector<float> packed(block.size_bytes / sizeof(float), 0.0f);
        size_t offset = 0;
        for (const CompiledAudioSpectrumMember& member : block.members) {
            const float* source = nullptr;
            if (member.name == "g_AudioSpectrum16Left")
                source = spectrum.bands16_left;
            else if (member.name == "g_AudioSpectrum16Right")
                source = spectrum.bands16_right;
            else if (member.name == "g_AudioSpectrum32Left")
                source = spectrum.bands32_left;
            else if (member.name == "g_AudioSpectrum32Right")
                source = spectrum.bands32_right;
            else if (member.name == "g_AudioSpectrum64Left")
                source = spectrum.bands64_left;
            else if (member.name == "g_AudioSpectrum64Right")
                source = spectrum.bands64_right;
            for (int i = 0; i < member.count; ++i) packed[offset + (size_t)i * 4] = source ? source[i] : 0.0f;
            offset += (size_t)member.count * 4;
        }
        sg_range range = {.ptr = packed.data(), .size = block.size_bytes};
        sg_apply_uniforms(block.slot, &range);
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

void ShaderPass::rebuildWithDebugMode(int mode, EngineContext& ctx) {
    debug_view_mode = mode;
    compiled = {};
    init(ctx);
}
