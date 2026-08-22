#include "scene_parser.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utility>

#include "shared/core/config.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"

namespace wallpaper_engine {
namespace {

const cJSON* propertyValue(const cJSON* node) {
    if (cJSON_IsObject(node)) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(node, "value");
        if (value) return value;
    }
    return node;
}

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

bool parseBool(const cJSON* raw, bool fallback = false) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsBool(node)) return cJSON_IsTrue(node);
    if (cJSON_IsNumber(node)) return node->valuedouble != 0.0;
    return fallback;
}

SceneNodeDocument parseNode(const cJSON* object) {
    SceneNodeDocument out;

    const cJSON* id = cJSON_GetObjectItemCaseSensitive(object, "id");
    if (!cJSON_IsNumber(id) || id->valuedouble <= 0.0) return out;

    out.valid = true;
    out.id = (uint32_t)id->valuedouble;

    const cJSON* parent = cJSON_GetObjectItemCaseSensitive(object, "parent");
    if (cJSON_IsNumber(parent) && parent->valuedouble > 0.0) {
        out.parent_id = (uint32_t)parent->valuedouble;
    }

    parseVec(cJSON_GetObjectItemCaseSensitive(object, "origin"), out.origin.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(object, "scale"), out.scale.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(object, "angles"), out.angles.data(), 3);

    out.has_parallax_depth =
        parseVec(cJSON_GetObjectItemCaseSensitive(object, "parallaxDepth"), out.parallax_depth.data(), 2);

    if (!out.has_parallax_depth) {
        const cJSON* image = cJSON_GetObjectItemCaseSensitive(object, "image");
        if (cJSON_IsString(image) && image->valuestring &&
            strcmp(image->valuestring, "models/util/composelayer.json") == 0) {
            out.parallax_depth = {1.0f, 1.0f};
        }
    }

    out.propagate_to_children = !parseBool(cJSON_GetObjectItemCaseSensitive(object, "disablepropagation"), false);
    return out;
}

SceneObjectKind detectObjectKind(const cJSON* object) {
    const cJSON* particle = cJSON_GetObjectItemCaseSensitive(object, "particle");
    if (cJSON_IsString(particle)) return SceneObjectKind::Particle;

    const cJSON* image = cJSON_GetObjectItemCaseSensitive(object, "image");
    const cJSON* model = cJSON_GetObjectItemCaseSensitive(object, "model");
    if (cJSON_IsString(image) || cJSON_IsString(model)) return SceneObjectKind::Image;

    return SceneObjectKind::Unknown;
}

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

    const bool is_hdr = parseBool(cJSON_GetObjectItemCaseSensitive(general, "hdr"), false);
    out.general.bloom.enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "bloom"), false) || is_hdr;
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

SceneObjectDocument parseObject(const cJSON* object) {
    SceneObjectDocument doc;
    doc.kind = detectObjectKind(object);
    doc.node = parseNode(object);

    const cJSON* name = cJSON_GetObjectItemCaseSensitive(object, "name");
    if (cJSON_IsString(name) && name->valuestring) {
        doc.name = name->valuestring;
    }

    doc.visible = parseBool(cJSON_GetObjectItemCaseSensitive(object, "visible"), true);

    const cJSON* image = cJSON_GetObjectItemCaseSensitive(object, "image");
    if (cJSON_IsString(image) && image->valuestring) {
        doc.image.image = image->valuestring;
    }

    const cJSON* model = cJSON_GetObjectItemCaseSensitive(object, "model");
    if (cJSON_IsString(model) && model->valuestring) {
        doc.image.model = model->valuestring;
    }

    parseVec(cJSON_GetObjectItemCaseSensitive(object, "size"), doc.image.size.data(), 2);
    parseVec(cJSON_GetObjectItemCaseSensitive(object, "color"), doc.image.color.data(), 3);
    const cJSON* alpha = cJSON_GetObjectItemCaseSensitive(object, "alpha");
    if (cJSON_IsNumber(alpha)) {
        doc.image.alpha = (float)alpha->valuedouble;
    } else if (cJSON_IsObject(alpha)) {
        // Animated properties retain their authoring-time fallback in `value`.
        // The animation program is separate from static image color semantics.
        parseFloat(cJSON_GetObjectItemCaseSensitive(alpha, "value"), doc.image.alpha);
        const cJSON* animation = cJSON_GetObjectItemCaseSensitive(alpha, "animation");
        if (cJSON_IsObject(animation)) {
            const cJSON* keys = cJSON_GetObjectItemCaseSensitive(animation, "c0");
            if (cJSON_IsArray(keys)) {
                const cJSON* key = nullptr;
                cJSON_ArrayForEach(key, keys) {
                    ImageObjectDocument::AlphaKey parsed;
                    if (!parseFloat(cJSON_GetObjectItemCaseSensitive(key, "frame"), parsed.frame) ||
                        !parseFloat(cJSON_GetObjectItemCaseSensitive(key, "value"), parsed.value))
                        continue;
                    doc.image.alpha_keys.push_back(parsed);
                }
            }
            const cJSON* options = cJSON_GetObjectItemCaseSensitive(animation, "options");
            if (cJSON_IsObject(options)) {
                parseFloat(cJSON_GetObjectItemCaseSensitive(options, "fps"), doc.image.alpha_fps);
                parseFloat(cJSON_GetObjectItemCaseSensitive(options, "length"), doc.image.alpha_length);
                const cJSON* mode = cJSON_GetObjectItemCaseSensitive(options, "mode");
                if (cJSON_IsString(mode) && mode->valuestring) doc.image.alpha_mode = mode->valuestring;
            }
        }
    }
    const cJSON* color_blend_mode = cJSON_GetObjectItemCaseSensitive(object, "colorBlendMode");
    if (cJSON_IsNumber(color_blend_mode)) doc.image.color_blend_mode = (int)color_blend_mode->valuedouble;
    doc.image.solid = parseBool(cJSON_GetObjectItemCaseSensitive(object, "solid"), false);
    doc.image.copy_background = parseBool(cJSON_GetObjectItemCaseSensitive(object, "copybackground"), false);

    const cJSON* particle = cJSON_GetObjectItemCaseSensitive(object, "particle");
    if (cJSON_IsString(particle) && particle->valuestring) {
        doc.particle.particle = particle->valuestring;
    }

    const cJSON* instance_override = cJSON_GetObjectItemCaseSensitive(object, "instanceoverride");
    if (cJSON_IsObject(instance_override)) {
        parseFloat(cJSON_GetObjectItemCaseSensitive(instance_override, "alpha"), doc.particle.override_alpha);
        parseFloat(cJSON_GetObjectItemCaseSensitive(instance_override, "rate"), doc.particle.override_rate);

        const cJSON* color = cJSON_GetObjectItemCaseSensitive(instance_override, "color");
        const cJSON* colorn = cJSON_GetObjectItemCaseSensitive(instance_override, "colorn");
        if (parseVec(color, doc.particle.override_color.data(), 3)) {
            doc.particle.has_override_color = true;
            doc.particle.override_color_is_legacy = true;
        } else if (parseVec(colorn, doc.particle.override_color.data(), 3)) {
            doc.particle.has_override_color = true;
            doc.particle.override_color_is_legacy = false;
        }
    }

    const cJSON* effects = cJSON_GetObjectItemCaseSensitive(object, "effects");
    if (cJSON_IsArray(effects)) {
        const cJSON* eff_json;
        cJSON_ArrayForEach(eff_json, effects) {
            const cJSON* file = cJSON_GetObjectItemCaseSensitive(eff_json, "file");
            if (cJSON_IsString(file) && file->valuestring) {
                EffectInstanceDocument eff_doc;
                eff_doc.file = file->valuestring;
                eff_doc.visible = parseBool(cJSON_GetObjectItemCaseSensitive(eff_json, "visible"), true);

                char* serialized = cJSON_PrintUnformatted(eff_json);
                if (serialized) {
                    eff_doc.instance_config_json = serialized;
                    cJSON_free(serialized);
                }
                doc.effects.push_back(std::move(eff_doc));
            }
        }
    }

    return doc;
}

}  // namespace

bool parseSceneFile(const char* scene_json_path, SceneDocument& out) {
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
    detectResolution(root, out);
    parseCamera(cJSON_GetObjectItemCaseSensitive(root, "camera"), out);
    parseGeneral(cJSON_GetObjectItemCaseSensitive(root, "general"), out);

    const cJSON* objects = cJSON_GetObjectItemCaseSensitive(root, "objects");
    if (cJSON_IsArray(objects)) {
        const cJSON* object;
        cJSON_ArrayForEach(object, objects) {
            out.objects.push_back(parseObject(object));
        }
    }

    cJSON_Delete(root);
    return true;
}

}  // namespace wallpaper_engine
