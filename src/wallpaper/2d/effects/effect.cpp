#include "effect.h"

#include <cstdlib>
#include <map>

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/passes/shader_pass.h"
#include "wallpaper/2d/parser/scene_document.h"

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
