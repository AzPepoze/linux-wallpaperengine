#include "effect_parser.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

void EffectConfiguration::mergeObject(cJSON*& target, const cJSON* source) {
    if (!cJSON_IsObject(source)) return;
    if (!target) target = cJSON_CreateObject();
    cJSON* item;
    cJSON_ArrayForEach(item, source) {
        if (!item->string) continue;
        cJSON_DeleteItemFromObjectCaseSensitive(target, item->string);
        cJSON_AddItemToObject(target, item->string, cJSON_Duplicate(item, 1));
    }
}

bool EffectConfiguration::readFloats(const cJSON* node, std::vector<float>& out) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        out.push_back((float)node->valuedouble);
        return true;
    }
    if (cJSON_IsBool(node)) {
        out.push_back(cJSON_IsTrue(node) ? 1.0f : 0.0f);
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        std::string text = node->valuestring;
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream stream(text);
        float value = 0.0f;
        bool parsed = false;
        while (stream >> value) {
            out.push_back(value);
            parsed = true;
        }
        return parsed;
    }
    if (cJSON_IsArray(node)) {
        bool parsed = false;
        cJSON* item;
        cJSON_ArrayForEach(item, node) {
            parsed = readFloats(item, out) || parsed;
        }
        return parsed;
    }
    return cJSON_IsObject(node) && readFloats(cJSON_GetObjectItemCaseSensitive(node, "value"), out);
}

bool EffectConfiguration::readInt(const cJSON* node, int& value) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        value = node->valueint;
        return true;
    }
    if (cJSON_IsBool(node)) {
        value = cJSON_IsTrue(node) ? 1 : 0;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        char* end = nullptr;
        const long parsed = strtol(node->valuestring, &end, 10);
        if (!end || end == node->valuestring) return false;
        value = (int)parsed;
        return true;
    }
    if (cJSON_IsArray(node) && cJSON_GetArraySize(node) > 0) return readInt(cJSON_GetArrayItem(node, 0), value);
    return cJSON_IsObject(node) && readInt(cJSON_GetObjectItemCaseSensitive(node, "value"), value);
}

void EffectConfiguration::readCombos(const cJSON* config, std::map<std::string, int>& combos) {
    const cJSON* combo_node = config ? cJSON_GetObjectItemCaseSensitive(config, "combos") : nullptr;
    if (!cJSON_IsObject(combo_node)) return;
    cJSON* item;
    cJSON_ArrayForEach(item, combo_node) {
        int value = 0;
        if (item->string && readInt(item, value)) combos[item->string] = value;
    }
}

void EffectConfiguration::readValuesObject(const cJSON* values, std::map<std::string, std::vector<float>>& uniforms) {
    if (!cJSON_IsObject(values)) return;
    cJSON* item;
    cJSON_ArrayForEach(item, values) {
        std::vector<float> parsed;
        if (item->string && readFloats(item, parsed) && !parsed.empty()) uniforms[item->string] = std::move(parsed);
    }
}

void EffectConfiguration::readUniformValues(const cJSON* config, std::map<std::string, std::vector<float>>& uniforms) {
    readValuesObject(config ? cJSON_GetObjectItemCaseSensitive(config, "constantshadervalues") : nullptr, uniforms);
}
