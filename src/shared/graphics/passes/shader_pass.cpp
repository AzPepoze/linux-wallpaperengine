#include "shader_pass.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <sstream>

#include "pass_loader.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/phase_timer.h"
#include "shared/core/task_pool.h"
#include "shared/core/utils.h"
#include "shared/core/vfs.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/shader/shader_processor.h"
#include "wallpaper/2d/effects/effect_parser.h"
#include "wallpaper/2d/layers/image/effect_resolution.h"

namespace {

bool readShaderStage(EngineContext& ctx, const char* relative_path, char* absolute_path, size_t capacity,
                     std::string& source) {
    if (!ctx.asset_mgr->resolvePath(relative_path, absolute_path, (int)capacity)) {
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", relative_path);
        if (!ctx.asset_mgr->resolvePath(extracted_path, absolute_path, (int)capacity)) return false;
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

}  // namespace

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx) {
    cJSON* base_config = config;
    cJSON* owned_base_config = nullptr;
    const cJSON* material_reference = cJSON_GetObjectItemCaseSensitive(config, "material");
    if (cJSON_IsString(material_reference) && material_reference->valuestring) {
        char material_path[1024];
        if (ctx.asset_mgr->resolvePath(material_reference->valuestring, material_path, sizeof(material_path))) {
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

void ShaderPass::init(EngineContext& ctx) {
    if (prepare(ctx, false)) finish(ctx);
}

void ShaderPass::initAsync(EngineContext& ctx) {
    const std::string engine_path = ctx.asset_mgr ? ctx.asset_mgr->getEnginePath() : std::string();
    const std::string wallpaper_path = ctx.asset_mgr ? ctx.asset_mgr->getWallpaperPath() : std::string();
    const vfs::PackageHandle package = vfs::currentPackage();
    std::shared_ptr<ShaderPass> job = makePreparationClone();
    pending_ = TaskPool::instance()
                   .enqueue([job = std::move(job), engine_path, wallpaper_path, package]() mutable {
                       vfs::ScopedBinding binding(package);
                       AssetManager assets;
                       assets.init(engine_path.c_str(), wallpaper_path.c_str());
                       EngineContext worker_ctx;
                       worker_ctx.asset_mgr = &assets;
                       if (!job->prepare(worker_ctx, true, false)) return std::shared_ptr<ShaderPass>();
                       return job;
                   })
                   .share();
}

void ShaderPass::completeInit(EngineContext& ctx) {
    if (!pending_.valid()) return;
    if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    std::shared_ptr<ShaderPass> prepared = pending_.get();
    pending_ = {};
    if (prepared) {
        adoptPreparationResult(*prepared);
        if (prepared_) {
            prepared_->sources.full_fs = renderObserver().overrideFragmentSource(
                shader_name, prepared_->sources.full_fs, debug_view_mode, debug_step);
            pixel_exact = pixel_exact || prepared_->sources.full_fs != stored_fs_source;
        }
        finish(ctx);
    }
}

bool ShaderPass::preparationReady() const {
    return !pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// Everything up to the compile: it reads files and processes text, never touches the GPU, so it can run on a worker.
// With `warm_cache` the SPIR-V is generated too, so finish() only has to create GPU objects.
bool ShaderPass::prepare(EngineContext& ctx, bool warm_cache, bool apply_observer) {
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
    sources.processed_vs = ShaderSourceProcessor::processShaderSource(sources.raw_vs, abs_vert, *ctx.asset_mgr, true);
    sources.processed_fs = ShaderSourceProcessor::processShaderSource(sources.raw_fs, abs_frag, *ctx.asset_mgr, false);

    pixel_exact = effect_resolution::readsPixelPosition(sources.processed_vs) ||
                  effect_resolution::readsPixelPosition(sources.processed_fs);
    {
        static const char* const kFrameVaryingBuiltins[] = {
            "g_Time", "g_Frametime", "g_ParallaxPosition", "g_PointerPosition", "g_PointerState", "g_AudioSpectrum"};
        frame_varying = false;
        for (const char* token : kFrameVaryingBuiltins) {
            if (sources.processed_vs.find(token) != std::string::npos ||
                sources.processed_fs.find(token) != std::string::npos) {
                frame_varying = true;
                break;
            }
        }
    }

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
    if (apply_observer)
        sources.full_fs =
            renderObserver().overrideFragmentSource(shader_name, sources.full_fs, debug_view_mode, debug_step);
    // A debug override replaces the fragment source, so its pixel reads are unknown.
    pixel_exact = pixel_exact || sources.full_fs != stored_fs_source;

    texture_labels = ShaderSourceProcessor::extractTextureLabels(sources.raw_fs.c_str());
    white_default_slots = ShaderSourceProcessor::extractWhiteTextureDefaults(sources.raw_fs.c_str());

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

std::shared_ptr<ShaderPass> ShaderPass::makePreparationClone() const {
    auto clone = std::shared_ptr<ShaderPass>(new ShaderPass());
    clone->shader_name = shader_name;
    clone->uniforms = uniforms;
    clone->combos = combos;
    clone->animated_uniforms = animated_uniforms;
    clone->render_target = render_target;
    clone->render_texture_bindings = render_texture_bindings;
    clone->debug_view_mode = debug_view_mode;
    clone->debug_step = debug_step;
    clone->pass_textures.textures.resize(pass_textures.textures.size());
    clone->preparation_texture_bound_.reserve(pass_textures.textures.size());
    for (const auto& image : pass_textures.textures)
        clone->preparation_texture_bound_.push_back(image.id != SG_INVALID_ID);
    return clone;
}

void ShaderPass::adoptPreparationResult(ShaderPass& prepared) {
    prepared_ = std::move(prepared.prepared_);
    uniforms = std::move(prepared.uniforms);
    texture_labels = std::move(prepared.texture_labels);
    white_default_slots = prepared.white_default_slots;
    pixel_exact = prepared.pixel_exact;
    frame_varying = prepared.frame_varying;
    is_fullscreen_quad = prepared.is_fullscreen_quad;
    geometry_classified = false;
    stored_vs_source = std::move(prepared.stored_vs_source);
    stored_fs_source = std::move(prepared.stored_fs_source);
    resolved_animations = std::move(prepared.resolved_animations);
    shader_uniform_configs_ = std::move(prepared.shader_uniform_configs_);
}

void ShaderPass::finish(EngineContext& ctx) {
    (void)ctx;
    const std::shared_ptr<PreparedShader> prepared = std::move(prepared_);
    if (!prepared) return;
    shader_uniform_configs_ = prepared->shader_uniforms;
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

bool ShaderPass::resolveMaterialName(const std::string& name, std::string& resolved) const {
    if (EffectParser::resolveUniformName(name, shader_uniform_configs_, resolved)) return true;
    if (uniforms.count(name)) {
        resolved = name;
        return true;
    }
    return false;
}

bool ShaderPass::setMaterialConstant(const std::string& name, const std::vector<float>& values) {
    std::string resolved;
    if (values.empty() || !resolveMaterialName(name, resolved)) return false;
    uniforms[resolved] = values;
    resolved_animations.erase(resolved);
    return true;
}

const std::vector<float>* ShaderPass::materialConstant(const std::string& name) const {
    std::string resolved;
    if (!resolveMaterialName(name, resolved)) return nullptr;
    const auto it = uniforms.find(resolved);
    return it == uniforms.end() ? nullptr : &it->second;
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
