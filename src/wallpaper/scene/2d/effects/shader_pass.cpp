#include <cstdlib>

#include "core/engine_context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "effect.h"
#include "wallpaper/scene/2d/parser/effect_parser.h"

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
#include <cstdlib>

#include "effect.h"

void ShaderPass::apply(EngineContext& ctx) {
    (void)ctx;
    if (!enabled || compiled.pipeline.id == SG_INVALID_ID) return;
}

void ShaderPass::applyUniforms() {
    int uniform_slot = 3;
    for (const auto& [name, values] : uniforms) {
        (void)name;
        if (uniform_slot >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        alignas(16) float data[4] = {0, 0, 0, 0};
        for (int index = 0; index < static_cast<int>(values.size()) && index < 4; ++index) data[index] = values[index];
        sg_range range = SG_RANGE(data);
        sg_apply_uniforms(uniform_slot, &range);
        ++uniform_slot;
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
