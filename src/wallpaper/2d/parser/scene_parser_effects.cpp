#include <cjson/cJSON.h>

#include <utility>

#include "scene_parser_internal.h"

namespace wallpaper_engine {
namespace scene_parser_detail {

namespace {

void parseEffectConstantScripts(const cJSON* effect_json, std::vector<EffectConstantScript>& out) {
    const cJSON* passes = member(effect_json, "passes");
    if (!cJSON_IsArray(passes)) return;
    int pass_index = 0;
    const cJSON* pass = nullptr;
    cJSON_ArrayForEach(pass, passes) {
        const cJSON* values = member(pass, "constantshadervalues");
        if (cJSON_IsObject(values)) {
            const cJSON* constant = nullptr;
            cJSON_ArrayForEach(constant, values) {
                if (!constant->string) continue;
                EffectConstantScript entry;
                entry.pass = pass_index;
                entry.name = constant->string;
                readScript(constant, entry.script);
                std::vector<PropertyAnimationDocument> animations;
                parsePropertyAnimation(values, constant->string, animations);
                if (!animations.empty()) {
                    entry.animation = std::move(animations.front());
                    entry.has_animation = true;
                }
                if (!entry.script.empty() || entry.has_animation) out.push_back(std::move(entry));
            }
        }
        ++pass_index;
    }
}

}  // namespace

void parseEffects(const cJSON* object, std::vector<EffectInstanceDocument>& out) {
    const cJSON* effects = member(object, "effects");
    if (!cJSON_IsArray(effects)) return;
    const cJSON* effect_json = nullptr;
    cJSON_ArrayForEach(effect_json, effects) {
        const cJSON* file = member(effect_json, "file");
        if (!cJSON_IsString(file) || !file->valuestring) continue;

        EffectInstanceDocument effect;
        effect.file = file->valuestring;
        const cJSON* name = member(effect_json, "name");
        if (cJSON_IsString(name) && name->valuestring) effect.name = name->valuestring;
        effect.visible = parseBool(member(effect_json, "visible"), true);
        readScript(member(effect_json, "visible"), effect.visible_script);
        if (char* serialized = cJSON_PrintUnformatted(effect_json)) {
            effect.instance_config_json = serialized;
            cJSON_free(serialized);
        }
        parseEffectConstantScripts(effect_json, effect.constant_scripts);
        out.push_back(std::move(effect));
    }
}

}  // namespace scene_parser_detail
}  // namespace wallpaper_engine
