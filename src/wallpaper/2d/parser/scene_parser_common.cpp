#include <cjson/cJSON.h>
#include <stdio.h>

#include <cstdlib>
#include <string>

#include "scene_parser_internal.h"

namespace wallpaper_engine {
namespace scene_parser_detail {

namespace {

const cJSON* propertyValue(const cJSON* node) {
    if (cJSON_IsObject(node)) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(node, "value");
        if (value) return value;
    }
    return node;
}

}  // namespace

bool parseVec(const cJSON* raw, float* out, int count) {
    const cJSON* node = propertyValue(raw);
    if (!node || !out || count <= 0) return false;

    if (cJSON_IsString(node) && node->valuestring) {
        if (count == 2) return sscanf(node->valuestring, "%f %f", &out[0], &out[1]) == 2;
        if (count == 3) return sscanf(node->valuestring, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
        return false;
    }

    if (cJSON_IsArray(node) && cJSON_GetArraySize(node) >= count) {
        for (int i = 0; i < count; ++i) {
            const cJSON* value = cJSON_GetArrayItem(node, i);
            if (!cJSON_IsNumber(value)) return false;
            out[i] = (float)value->valuedouble;
        }
        return true;
    }

    return false;
}

bool parseFloat(const cJSON* raw, float& out) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsNumber(node)) {
        out = (float)node->valuedouble;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        char* end = nullptr;
        const float value = strtof(node->valuestring, &end);
        if (end != node->valuestring) {
            out = value;
            return true;
        }
    }
    return false;
}

bool parseBool(const cJSON* raw, bool fallback) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsBool(node)) return cJSON_IsTrue(node);
    if (cJSON_IsNumber(node)) return node->valuedouble != 0.0;
    return fallback;
}

bool parseString(const cJSON* raw, std::string& out) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsString(node) && node->valuestring) {
        out = node->valuestring;
        return true;
    }
    return false;
}

// Reads `script` and `scriptproperties` from a property object; leaves `out` empty for plain values.
void readScript(const cJSON* property, ScriptedValue& out) {
    if (!cJSON_IsObject(property)) return;
    const cJSON* script = cJSON_GetObjectItemCaseSensitive(property, "script");
    if (!cJSON_IsString(script) || !script->valuestring || !script->valuestring[0]) return;
    out.script = script->valuestring;
    const cJSON* properties = cJSON_GetObjectItemCaseSensitive(property, "scriptproperties");
    if (cJSON_IsObject(properties)) {
        if (char* printed = cJSON_PrintUnformatted(properties)) {
            out.properties_json = printed;
            cJSON_free(printed);
        }
    }
}
const cJSON* member(const cJSON* object, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

void readPlainString(const cJSON* object, const char* key, std::string& out) {
    const cJSON* node = member(object, key);
    if (cJSON_IsString(node) && node->valuestring) out = node->valuestring;
}

}  // namespace scene_parser_detail
}  // namespace wallpaper_engine
