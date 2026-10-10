#include "wallpaper/live_fields.h"

#include <cjson/cJSON.h>

std::string jsonWithout(const std::string& json, std::initializer_list<const char*> keys) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return json;
    for (const char* key : keys) cJSON_DeleteItemFromObjectCaseSensitive(root, key);
    char* printed = cJSON_PrintUnformatted(root);
    std::string result = printed ? printed : json;
    cJSON_free(printed);
    cJSON_Delete(root);
    return result;
}
