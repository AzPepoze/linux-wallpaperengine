#include "app/control/control_protocol.h"

#include <cjson/cJSON.h>

namespace {
// cJSON's print owns the buffer; copy it out and free.
std::string printAndFree(cJSON* node) {
    if (!node) return {};
    char* text = cJSON_PrintUnformatted(node);
    std::string result = text ? text : "";
    cJSON_free(text);
    return result;
}

std::string readString(const cJSON* node) {
    if (!cJSON_IsString(node) || !node->valuestring) return {};
    return node->valuestring;
}
}  // namespace

std::string encodeSwitchRequest(const SwitchRequest& request) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return {};
    cJSON_AddStringToObject(root, "path", request.path.c_str());
    cJSON_AddBoolToObject(root, "is_pkg", request.is_pkg);

    cJSON* properties = cJSON_AddObjectToObject(root, "properties");
    if (properties) {
        for (const auto& [key, value] : request.properties) {
            cJSON_AddStringToObject(properties, key.c_str(), value.c_str());
        }
    }

    cJSON_AddNumberToObject(root, "transition", request.transition);
    cJSON_AddNumberToObject(root, "transition_time_ms", request.transition_time_ms);

    std::string result = printAndFree(root);
    cJSON_Delete(root);
    return result;
}

bool decodeSwitchRequest(const std::string& json, SwitchRequest& out, std::string& error) {
    if (json.empty()) {
        error = "empty request";
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) {
        error = "invalid JSON";
        return false;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        error = "request must be a JSON object";
        return false;
    }

    SwitchRequest parsed;
    parsed.path = readString(cJSON_GetObjectItemCaseSensitive(root, "path"));
    if (parsed.path.empty()) {
        cJSON_Delete(root);
        error = "missing path";
        return false;
    }

    if (const cJSON* is_pkg = cJSON_GetObjectItemCaseSensitive(root, "is_pkg"); cJSON_IsBool(is_pkg)) {
        parsed.is_pkg = cJSON_IsTrue(is_pkg);
    }

    if (const cJSON* properties = cJSON_GetObjectItemCaseSensitive(root, "properties");
        cJSON_IsObject(properties)) {
        for (const cJSON* entry = properties->child; entry; entry = entry->next) {
            if (cJSON_IsString(entry) && entry->string && entry->valuestring) {
                parsed.properties.emplace_back(entry->string, entry->valuestring);
            }
        }
    }

    if (const cJSON* transition = cJSON_GetObjectItemCaseSensitive(root, "transition");
        cJSON_IsNumber(transition)) {
        parsed.transition = (int)transition->valuedouble;
    }
    if (const cJSON* duration = cJSON_GetObjectItemCaseSensitive(root, "transition_time_ms");
        cJSON_IsNumber(duration)) {
        parsed.transition_time_ms = (int)duration->valuedouble;
    }

    cJSON_Delete(root);
    out = std::move(parsed);
    error.clear();
    return true;
}

std::string encodeReply(bool ok, const std::string& error) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return {};
    cJSON_AddBoolToObject(root, "ok", ok);
    if (!ok) cJSON_AddStringToObject(root, "error", error.c_str());
    std::string result = printAndFree(root);
    cJSON_Delete(root);
    return result;
}
