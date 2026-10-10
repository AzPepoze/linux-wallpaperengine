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
    cJSON_AddStringToObject(root, "transition_mode", request.continue_previous ? "continue" : "freeze");

    // Only emit optional fields when set; absent means "keep current".
    if (!request.scaling.empty()) cJSON_AddStringToObject(root, "scaling", request.scaling.c_str());
    if (request.has_volume) cJSON_AddNumberToObject(root, "volume", request.volume);
    if (request.has_muted) cJSON_AddBoolToObject(root, "muted", request.muted);
    if (!request.audio_device.empty()) cJSON_AddStringToObject(root, "audio_device", request.audio_device.c_str());
    if (request.has_audio_processing) cJSON_AddBoolToObject(root, "audio_processing", request.audio_processing);
    if (request.has_fps) cJSON_AddNumberToObject(root, "fps", request.fps);
    if (request.toggle_debug_ui) cJSON_AddBoolToObject(root, "toggle_debug_ui", true);

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

    if (const cJSON* is_pkg = cJSON_GetObjectItemCaseSensitive(root, "is_pkg"); cJSON_IsBool(is_pkg)) {
        parsed.is_pkg = cJSON_IsTrue(is_pkg);
    }

    if (const cJSON* properties = cJSON_GetObjectItemCaseSensitive(root, "properties"); cJSON_IsObject(properties)) {
        for (const cJSON* entry = properties->child; entry; entry = entry->next) {
            if (cJSON_IsString(entry) && entry->string && entry->valuestring) {
                parsed.properties.emplace_back(entry->string, entry->valuestring);
            }
        }
    }

    if (const cJSON* transition = cJSON_GetObjectItemCaseSensitive(root, "transition"); cJSON_IsNumber(transition)) {
        parsed.transition = (int)transition->valuedouble;
    }
    if (const cJSON* duration = cJSON_GetObjectItemCaseSensitive(root, "transition_time_ms");
        cJSON_IsNumber(duration)) {
        parsed.transition_time_ms = (int)duration->valuedouble;
    }
    if (const cJSON* mode = cJSON_GetObjectItemCaseSensitive(root, "transition_mode"); cJSON_IsString(mode)) {
        lwe::transition::Mode parsed_mode = lwe::transition::Mode::Freeze;
        if (!lwe::transition::parseMode(mode->valuestring, parsed_mode)) {
            error = std::string("unknown transition_mode '") + mode->valuestring + "'";
            cJSON_Delete(root);
            return false;
        }
        parsed.continue_previous = parsed_mode == lwe::transition::Mode::Continue;
    }

    if (const cJSON* scaling = cJSON_GetObjectItemCaseSensitive(root, "scaling"); cJSON_IsString(scaling)) {
        parsed.scaling = scaling->valuestring ? scaling->valuestring : "";
    }
    if (const cJSON* volume = cJSON_GetObjectItemCaseSensitive(root, "volume"); cJSON_IsNumber(volume)) {
        parsed.volume = static_cast<float>(volume->valuedouble);
        parsed.has_volume = true;
    }
    if (const cJSON* muted = cJSON_GetObjectItemCaseSensitive(root, "muted"); cJSON_IsBool(muted)) {
        parsed.muted = cJSON_IsTrue(muted);
        parsed.has_muted = true;
    }
    parsed.audio_device = readString(cJSON_GetObjectItemCaseSensitive(root, "audio_device"));
    if (const cJSON* processing = cJSON_GetObjectItemCaseSensitive(root, "audio_processing");
        cJSON_IsBool(processing)) {
        parsed.audio_processing = cJSON_IsTrue(processing);
        parsed.has_audio_processing = true;
    }

    // A path is required unless the request only changes audio or volume settings.
    const bool has_settings =
        parsed.has_volume || parsed.has_muted || parsed.has_audio_processing || !parsed.audio_device.empty();
    if (parsed.path.empty() && !has_settings) {
        cJSON_Delete(root);
        error = "missing path";
        return false;
    }
    if (const cJSON* fps = cJSON_GetObjectItemCaseSensitive(root, "fps"); cJSON_IsNumber(fps)) {
        parsed.fps = static_cast<int>(fps->valuedouble);
        parsed.has_fps = true;
    }
    if (const cJSON* toggle = cJSON_GetObjectItemCaseSensitive(root, "toggle_debug_ui"); cJSON_IsBool(toggle)) {
        parsed.toggle_debug_ui = cJSON_IsTrue(toggle);
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
