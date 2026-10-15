#include "wallpaper/live_fields.h"

#include <cjson/cJSON.h>

namespace {
// Serialises `root` back to JSON, falling back to `json` if printing fails.
std::string printJson(cJSON* root, const std::string& json) {
    char* printed = cJSON_PrintUnformatted(root);
    std::string result = printed ? printed : json;
    cJSON_free(printed);
    return result;
}

void removeScriptProperties(cJSON* node) {
    if (cJSON_IsObject(node)) cJSON_DeleteItemFromObjectCaseSensitive(node, "scriptproperties");
    for (cJSON* child = node->child; child; child = child->next) removeScriptProperties(child);
}
}  // namespace

std::string jsonWithout(const std::string& json, std::initializer_list<const char*> keys) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return json;
    for (const char* key : keys) cJSON_DeleteItemFromObjectCaseSensitive(root, key);
    std::string result = printJson(root, json);
    cJSON_Delete(root);
    return result;
}

std::string jsonWithoutScriptProperties(const std::string& json) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return json;
    removeScriptProperties(root);
    std::string result = printJson(root, json);
    cJSON_Delete(root);
    return result;
}

namespace {
// Drops the constant values of every pass of one effect instance.
void removePassConstants(cJSON* effect) {
    cJSON* passes = cJSON_GetObjectItemCaseSensitive(effect, "passes");
    if (!cJSON_IsArray(passes)) return;
    cJSON* pass = nullptr;
    cJSON_ArrayForEach(pass, passes) {
        if (cJSON_IsObject(pass)) cJSON_DeleteItemFromObjectCaseSensitive(pass, "constantshadervalues");
    }
}
}  // namespace

std::string jsonWithoutPassConstants(const std::string& instance_json) {
    cJSON* root = cJSON_Parse(instance_json.c_str());
    if (!root) return instance_json;
    removePassConstants(root);
    std::string result = printJson(root, instance_json);
    cJSON_Delete(root);
    return result;
}

std::string jsonWithoutEffectConstants(const std::string& object_json) {
    cJSON* root = cJSON_Parse(object_json.c_str());
    if (!root) return object_json;
    cJSON* effects = cJSON_GetObjectItemCaseSensitive(root, "effects");
    if (cJSON_IsArray(effects)) {
        cJSON* effect = nullptr;
        cJSON_ArrayForEach(effect, effects) {
            if (cJSON_IsObject(effect)) removePassConstants(effect);
        }
    }
    std::string result = printJson(root, object_json);
    cJSON_Delete(root);
    return result;
}

std::string jsonWithoutInstanceOverrides(const std::string& object_json) {
    cJSON* root = cJSON_Parse(object_json.c_str());
    if (!root) return object_json;
    cJSON* overrides = cJSON_GetObjectItemCaseSensitive(root, "instanceoverride");
    if (cJSON_IsObject(overrides)) {
        for (const char* key : {"alpha", "rate", "size", "count", "speed", "lifetime", "color", "colorn"})
            cJSON_DeleteItemFromObjectCaseSensitive(overrides, key);
    }
    std::string result = printJson(root, object_json);
    cJSON_Delete(root);
    return result;
}

std::string jsonWithoutLiveGeneral(const std::string& json, bool zoom_live) {
    // Bloom on/off and clear enable are not listed: they need render targets set up at load.
    const std::string without_live =
        jsonWithout(json, {"cameraparallax", "cameraparallaxamount", "cameraparallaxdelay",
                           "cameraparallaxmouseinfluence", "camerashake", "camerashakeamplitude", "camerashakespeed",
                           "camerashakeroughness", "bloomstrength", "bloomthreshold", "ambientcolor", "skylightcolor",
                           "clearcolor"});
    return zoom_live ? jsonWithout(without_live, {"zoom"}) : without_live;
}
