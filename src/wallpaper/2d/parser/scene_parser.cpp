#include "scene_parser.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

#include "scene_parser_internal.h"
#include "shared/core/config.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "wallpaper/user_properties.h"

namespace wallpaper_engine {

using namespace scene_parser_detail;

namespace {

void detectResolution(const cJSON* root, SceneDocument& out) {
    const cJSON* resolution = cJSON_GetObjectItemCaseSensitive(root, "resolution");
    if (cJSON_IsString(resolution)) {
        sscanf(resolution->valuestring, "%f %f", &out.design_width, &out.design_height);
    }

    const cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    if (out.design_width == 0.0f && general) {
        const cJSON* ortho = cJSON_GetObjectItemCaseSensitive(general, "orthogonalprojection");
        if (ortho) {
            const cJSON* width = cJSON_GetObjectItemCaseSensitive(ortho, "width");
            const cJSON* height = cJSON_GetObjectItemCaseSensitive(ortho, "height");
            if (cJSON_IsNumber(width)) out.design_width = (float)width->valuedouble;
            if (cJSON_IsNumber(height)) out.design_height = (float)height->valuedouble;
        }
    }

    if (out.design_width == 0.0f) {
        out.design_width = Config::kDefaultSceneWidth;
        out.design_height = Config::kDefaultSceneHeight;
        LOG_W("Design resolution not found, defaulting to %dx%d", (int)out.design_width, (int)out.design_height);
    } else {
        LOG_I("Detected Design Resolution: %.0fx%.0f", out.design_width, out.design_height);
    }
}

void parseCamera(const cJSON* camera, SceneDocument& out) {
    if (!camera) return;
    parseVec(cJSON_GetObjectItemCaseSensitive(camera, "center"), out.camera.center.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(camera, "eye"), out.camera.eye.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(camera, "up"), out.camera.up.data(), 3);
}

void parseGeneral(const cJSON* general, SceneDocument& out) {
    if (!general) return;

    parseVec(cJSON_GetObjectItemCaseSensitive(general, "ambientcolor"), out.general.ambient_color.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(general, "skylightcolor"), out.general.skylight_color.data(), 3);

    const cJSON* clear_color = cJSON_GetObjectItemCaseSensitive(general, "clearcolor");
    if (cJSON_IsString(clear_color) && clear_color->valuestring) {
        float r, g, b;
        if (sscanf(clear_color->valuestring, "%f %f %f", &r, &g, &b) == 3) {
            out.general.clear_color = {r, g, b, 1.0f};
            out.general.has_clear_color = true;
        }
    }

    out.general.clear_enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "clearenabled"), true);
    out.general.hdr = parseBool(cJSON_GetObjectItemCaseSensitive(general, "hdr"), true);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "zoom"), out.general.zoom);
    readScript(cJSON_GetObjectItemCaseSensitive(general, "zoom"), out.general.zoom_script);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "fov"), out.general.fov);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "nearz"), out.general.near_z);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "farz"), out.general.far_z);
    out.general.camera_fade = parseBool(cJSON_GetObjectItemCaseSensitive(general, "camerafade"), true);
    out.general.camera_preview = parseBool(cJSON_GetObjectItemCaseSensitive(general, "camerapreview"), true);

    out.general.camera_parallax_enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "cameraparallax"), false);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxamount"), out.general.camera_parallax_amount);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxdelay"), out.general.camera_parallax_delay);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxmouseinfluence"),
               out.general.camera_parallax_mouse_influence);

    out.general.camera_shake_enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "camerashake"), false);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "camerashakeamplitude"), out.general.camera_shake_amplitude);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "camerashakespeed"), out.general.camera_shake_speed);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "camerashakeroughness"), out.general.camera_shake_roughness);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "perspectiveoverridefov"),
               out.general.perspective_override_fov);

    const cJSON* ortho = cJSON_GetObjectItemCaseSensitive(general, "orthogonalprojection");
    if (cJSON_IsObject(ortho)) {
        parseFloat(cJSON_GetObjectItemCaseSensitive(ortho, "width"), out.general.orthogonal_projection[0]);
        parseFloat(cJSON_GetObjectItemCaseSensitive(ortho, "height"), out.general.orthogonal_projection[1]);
    }

    out.general.bloom.enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "bloom"), false);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomstrength"), out.general.bloom.strength);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomthreshold"), out.general.bloom.threshold);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrfeather"), out.general.bloom.hdr_feather);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdriterations"), out.general.bloom.hdr_iterations);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrscatter"), out.general.bloom.hdr_scatter);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrstrength"), out.general.bloom.hdr_strength);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrthreshold"), out.general.bloom.hdr_threshold);

    LOG_I("Camera Parallax: %s, amount=%.3f, delay=%.3f, mouse influence=%.3f",
          out.general.camera_parallax_enabled ? "enabled" : "disabled", out.general.camera_parallax_amount,
          out.general.camera_parallax_delay, out.general.camera_parallax_mouse_influence);
    LOG_I("Camera Shake: %s, amplitude=%.3f, speed=%.3f, roughness=%.3f",
          out.general.camera_shake_enabled ? "enabled" : "disabled", out.general.camera_shake_amplitude,
          out.general.camera_shake_speed, out.general.camera_shake_roughness);
    LOG_I("Scene General: ambient=[%.2f, %.2f, %.2f], skylight=[%.2f, %.2f, %.2f], fov=%.1f, zoom=%.2f, bloom=%s",
          out.general.ambient_color[0], out.general.ambient_color[1], out.general.ambient_color[2],
          out.general.skylight_color[0], out.general.skylight_color[1], out.general.skylight_color[2], out.general.fov,
          out.general.zoom, out.general.bloom.enabled ? "enabled" : "disabled");
}

cJSON* userValueJson(const UserPropertyValue& value) {
    switch (value.type) {
        case UserPropertyValue::Type::Bool:
            return cJSON_CreateBool(value.b);
        case UserPropertyValue::Type::Number:
            return cJSON_CreateNumber(value.n);
        case UserPropertyValue::Type::Color:
        case UserPropertyValue::Type::Text:
            return cJSON_CreateString(value.asText().c_str());
    }
    return cJSON_CreateNull();
}

void addBoundKey(std::vector<std::string>& keys, const std::string& key) {
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
}

// A condition turns the value into a boolean: true while the property equals it.
// Every key a binding reads is added to `keys`, so a later change can tell whether the scene must rebuild.
void resolveUserBindings(cJSON* node, const UserProperties& properties, std::vector<std::string>& keys) {
    if (cJSON_IsObject(node)) {
        const cJSON* user = cJSON_GetObjectItemCaseSensitive(node, "user");
        std::string key, condition;
        bool has_condition = false;
        if (cJSON_IsString(user) && user->valuestring) {
            key = user->valuestring;
        } else if (cJSON_IsObject(user)) {
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(user, "name");
            const cJSON* cond = cJSON_GetObjectItemCaseSensitive(user, "condition");
            if (cJSON_IsString(name) && name->valuestring) key = name->valuestring;
            if (cJSON_IsString(cond) && cond->valuestring) {
                has_condition = true;
                condition = cond->valuestring;
            } else if (cJSON_IsNumber(cond)) {
                has_condition = true;
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%g", cond->valuedouble);
                condition = buffer;
            }
        }
        if (!key.empty()) addBoundKey(keys, key);
        if (const UserPropertyValue* value = key.empty() ? nullptr : properties.find(key)) {
            cJSON* replacement = has_condition ? cJSON_CreateBool(value->asText() == condition) : userValueJson(*value);
            if (cJSON_HasObjectItem(node, "value"))
                cJSON_ReplaceItemInObjectCaseSensitive(node, "value", replacement);
            else
                cJSON_AddItemToObject(node, "value", replacement);
        }
    }
    for (cJSON* child = node ? node->child : nullptr; child; child = child->next)
        resolveUserBindings(child, properties, keys);
}

}  // namespace

bool parseSceneObject(const char* object_json, SceneObjectDocument& out) {
    cJSON* root = cJSON_Parse(object_json);
    if (!root) return false;
    out = parseObject(root);
    cJSON_Delete(root);
    return out.node.valid;
}

bool parseSceneFile(const char* scene_json_path, SceneDocument& out, const UserProperties* user_properties) {
    char* json_str = read_file_to_string(scene_json_path);
    if (!json_str) {
        LOG_E("Failed to read scene JSON: %s", scene_json_path);
        return false;
    }

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        LOG_E("Failed to parse scene JSON");
        return false;
    }

    LOG_I("Scene JSON parsed successfully");
    if (user_properties) resolveUserBindings(root, *user_properties, out.user_keys);
    detectResolution(root, out);
    parseCamera(cJSON_GetObjectItemCaseSensitive(root, "camera"), out);
    parseGeneral(cJSON_GetObjectItemCaseSensitive(root, "general"), out);

    const cJSON* objects = cJSON_GetObjectItemCaseSensitive(root, "objects");
    if (cJSON_IsArray(objects)) {
        const cJSON* object;
        cJSON_ArrayForEach(object, objects) {
            parseCameraPath(object, out.camera);
            out.objects.push_back(parseObject(object));
        }
    }

    cJSON_Delete(root);
    return true;
}

}  // namespace wallpaper_engine
