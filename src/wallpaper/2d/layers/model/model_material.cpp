#include "model_material.h"

#include <ctype.h>
#include <string.h>

namespace {

const char* stringField(const cJSON* pass, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(pass, key);
    return cJSON_IsString(value) ? value->valuestring : nullptr;
}

bool fieldEquals(const cJSON* pass, const char* key, const char* expected) {
    const char* value = stringField(pass, key);
    return value && strcmp(value, expected) == 0;
}

ShaderBlendMode blendMode(const cJSON* pass) {
    if (fieldEquals(pass, "blending", "translucent")) return ShaderBlendMode::Alpha;
    if (fieldEquals(pass, "blending", "additive")) return ShaderBlendMode::Additive;
    return ShaderBlendMode::Disabled;
}

bool propertyValues(const UserPropertyValue& value, std::vector<float>& out) {
    switch (value.type) {
        case UserPropertyValue::Type::Color:
            out.assign(value.color, value.color + 3);
            return true;
        case UserPropertyValue::Type::Number:
            out.assign(1, (float)value.n);
            return true;
        case UserPropertyValue::Type::Bool:
            out.assign(1, value.b ? 1.0f : 0.0f);
            return true;
        case UserPropertyValue::Type::Text:
            return false;
    }
    return false;
}

std::string upperCase(std::string text) {
    for (char& c : text) c = (char)toupper((unsigned char)c);
    return text;
}

}  // namespace

MeshPipelineState parseMeshPipelineState(const cJSON* pass) {
    MeshPipelineState state;
    state.blend = blendMode(pass);
    state.cull_back_faces = !fieldEquals(pass, "cullmode", "nocull");
    state.depth_test = !fieldEquals(pass, "depthtest", "disabled");
    state.depth_write = !fieldEquals(pass, "depthwrite", "disabled");
    return state;
}

std::map<std::string, std::vector<float>> resolveUserShaderValues(const cJSON* pass, const UserProperties& properties) {
    std::map<std::string, std::vector<float>> values;
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(pass, "usershadervalues")) {
        if (!entry->string || !cJSON_IsString(entry) || !entry->valuestring) continue;
        const UserPropertyValue* property = properties.find(entry->string);
        std::vector<float> resolved;
        if (property && propertyValues(*property, resolved)) values[entry->valuestring] = std::move(resolved);
    }
    return values;
}

uint32_t textureSlotsNamed(const cJSON* pass, const char* texture) {
    uint32_t slots = 0;
    int slot = 0;
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(pass, "textures")) {
        if (slot < 32 && cJSON_IsString(entry) && strcmp(entry->valuestring, texture) == 0) slots |= 1u << slot;
        ++slot;
    }
    return slots;
}

std::map<std::string, int> upperCaseCombos(const std::map<std::string, int>& combos) {
    std::map<std::string, int> upper;
    for (const auto& [name, value] : combos) upper[upperCase(name)] = value;
    return upper;
}
