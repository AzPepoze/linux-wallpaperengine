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

// Texture slot N (g_TextureN) of the sampler annotated `"combo":"MASK"`, or -1.
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
        if (slot < preparation_texture_bound_.size()) return preparation_texture_bound_[slot];
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
