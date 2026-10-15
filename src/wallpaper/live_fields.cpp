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
