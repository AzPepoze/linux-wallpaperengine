#include "wallpaper/user_properties.h"

#include <cjson/cJSON.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "shared/core/vfs.h"

namespace {
std::string readFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!vfs::readAll(path.c_str(), bytes)) return "";
    return std::string(bytes.begin(), bytes.end());
}

std::string lowered(const char* text) {
    std::string out = text ? text : "";
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

bool parseBool(const std::string& raw, bool& out) {
    const std::string value = lowered(raw.c_str());
    if (value == "1" || value == "true") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false") {
        out = false;
        return true;
    }
    return false;
}

bool parseNumber(const std::string& raw, double& out) {
    if (raw.empty()) return false;
    char* end = nullptr;
    out = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str()) return false;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    return *end == '\0';
}

// A color arrives as "r g b"; authored values above 1 are a 0..255 triple.
bool parseColor(const std::string& raw, float out[3]) {
    std::istringstream in(raw);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (!(in >> r >> g >> b)) return false;
    if (r > 1.0f || g > 1.0f || b > 1.0f) {
        r /= 255.0f;
        g /= 255.0f;
        b /= 255.0f;
    }
    out[0] = r;
    out[1] = g;
    out[2] = b;
    return true;
}

std::string jsonText(const cJSON* value) {
    if (!value) return "";
    if (cJSON_IsString(value) && value->valuestring) return value->valuestring;
    if (cJSON_IsBool(value)) return cJSON_IsTrue(value) ? "true" : "false";
    if (cJSON_IsNumber(value)) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.17g", value->valuedouble);
        return buffer;
    }
    return "";
}

UserPropertyValue parseRaw(const std::string& type, const std::string& raw) {
    UserPropertyValue value;
    const std::string kind = lowered(type.c_str());
    if (kind == "bool" && parseBool(raw, value.b)) {
        value.type = UserPropertyValue::Type::Bool;
        return value;
    }
    if ((kind == "slider" || kind == "combo") && parseNumber(raw, value.n)) {
        value.type = UserPropertyValue::Type::Number;
        return value;
    }
    if (kind == "color" && parseColor(raw, value.color)) {
        value.type = UserPropertyValue::Type::Color;
        return value;
    }
    value.type = UserPropertyValue::Type::Text;
    value.text = raw;
    return value;
}

UserPropertyValue defaultValue(const std::string& type, const cJSON* value) {
    return parseRaw(type, jsonText(value));
}
}  // namespace

bool UserProperties::loadProject(const std::string& project_json_path) {
    properties_.clear();
    const std::string text = readFile(project_json_path);
    if (text.empty()) return false;
    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) return false;

    const cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    const cJSON* properties =
        cJSON_IsObject(general) ? cJSON_GetObjectItemCaseSensitive(general, "properties") : nullptr;
    if (cJSON_IsObject(properties)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, properties) {
            if (!item->string) continue;
            UserPropertyDef def;
            def.key = item->string;
            const cJSON* type = cJSON_GetObjectItemCaseSensitive(item, "type");
            if (cJSON_IsString(type) && type->valuestring) def.type = type->valuestring;
            def.value = defaultValue(def.type, cJSON_GetObjectItemCaseSensitive(item, "value"));
            properties_.push_back(std::move(def));
        }
    }
    cJSON_Delete(root);
    return true;
}

void UserProperties::applyPreset(const std::string& preset_json_path, const std::string& preset_root) {
    const std::string text = readFile(preset_json_path);
    cJSON* root = text.empty() ? nullptr : cJSON_Parse(text.c_str());
    if (!root) return;

    const cJSON* preset = cJSON_GetObjectItemCaseSensitive(root, "preset");
    if (cJSON_IsObject(preset)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, preset) {
            if (!item->string || cJSON_IsNull(item)) continue;
            for (UserPropertyDef& def : properties_) {
                if (def.key != item->string) continue;
                std::string raw = jsonText(item);
                // Texture values are stored beside the preset, so they resolve from its folder.
                if (def.type == "scenetexture" && !raw.empty() && raw[0] != '/') raw = preset_root + "/" + raw;
                def.value = parseRaw(def.type, raw);
                break;
            }
        }
    }
    cJSON_Delete(root);
}

void UserProperties::applySaved(const std::string& gui_config_json_text, const std::string& workshop_id) {
    if (gui_config_json_text.empty() || workshop_id.empty()) return;
    cJSON* root = cJSON_Parse(gui_config_json_text.c_str());
    if (!root) return;

    const cJSON* all = cJSON_GetObjectItemCaseSensitive(root, "wallpaperProperties");
    const cJSON* saved = cJSON_IsObject(all) ? cJSON_GetObjectItemCaseSensitive(all, workshop_id.c_str()) : nullptr;
    if (cJSON_IsObject(saved)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, saved) {
            if (item->string) setFromString(item->string, jsonText(item));
        }
    }
    cJSON_Delete(root);
}

void UserProperties::setFromString(const std::string& key, const std::string& raw) {
    for (UserPropertyDef& def : properties_) {
        if (def.key == key) {
            def.value = parseRaw(def.type, raw);
            return;
        }
    }
    UserPropertyDef def;
    def.key = key;
    def.type = "textinput";
    def.value = parseRaw(def.type, raw);
    properties_.push_back(std::move(def));
}

const UserPropertyValue* UserProperties::find(const std::string& key) const {
    for (const UserPropertyDef& def : properties_) {
        if (def.key == key) return &def.value;
    }
    return nullptr;
}

const std::string* UserProperties::texturePath(const std::string& key) const {
    const UserPropertyValue* value = find(key);
    if (!value || value->type != UserPropertyValue::Type::Text || value->text.empty()) return nullptr;
    return &value->text;
}

std::vector<std::pair<std::string, std::string>> UserProperties::toStrings() const {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(properties_.size());
    for (const UserPropertyDef& def : properties_) out.emplace_back(def.key, def.value.asText());
    return out;
}

bool touchesBoundKey(const std::vector<std::string>& bound_keys,
                     const std::vector<std::pair<std::string, std::string>>& changes) {
    for (const auto& change : changes) {
        if (std::find(bound_keys.begin(), bound_keys.end(), change.first) != bound_keys.end()) return true;
    }
    return false;
}

std::string UserPropertyValue::asText() const {
    char buffer[64];
    switch (type) {
        case Type::Bool:
            return b ? "1" : "0";
        case Type::Number:
            std::snprintf(buffer, sizeof(buffer), "%g", n);
            return buffer;
        case Type::Color:
            std::snprintf(buffer, sizeof(buffer), "%g %g %g", color[0], color[1], color[2]);
            return buffer;
        case Type::Text:
            return text;
    }
    return "";
}
