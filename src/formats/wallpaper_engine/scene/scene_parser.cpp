#include "scene_parser.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utility>

#include "../../../core/config.h"
#include "../../../core/logger.h"
#include "../../../core/utils.h"

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

void parseGeneral(const cJSON* general, SceneDocument& out) {
    if (!general) return;

    const cJSON* clear_color = cJSON_GetObjectItemCaseSensitive(general, "clearcolor");
    if (cJSON_IsString(clear_color)) {
        float r, g, b;
        if (sscanf(clear_color->valuestring, "%f %f %f", &r, &g, &b) == 3) {
            out.clear_color = {r, g, b, 1.0f};
            out.has_clear_color = true;
        }
    }

    const cJSON* camera_parallax = cJSON_GetObjectItemCaseSensitive(general, "cameraparallax");
    if (cJSON_IsBool(camera_parallax)) {
        out.camera_parallax_enabled = cJSON_IsTrue(camera_parallax);
    } else if (cJSON_IsObject(camera_parallax)) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(camera_parallax, "value");
        if (cJSON_IsBool(value)) out.camera_parallax_enabled = cJSON_IsTrue(value);
    }

    const cJSON* amount = cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxamount");
    if (cJSON_IsNumber(amount)) out.camera_parallax_amount = (float)amount->valuedouble;

    const cJSON* delay = cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxdelay");
    if (cJSON_IsNumber(delay)) out.camera_parallax_delay = (float)delay->valuedouble;

    const cJSON* mouse_influence = cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxmouseinfluence");
    if (cJSON_IsNumber(mouse_influence)) {
        out.camera_parallax_mouse_influence = (float)mouse_influence->valuedouble;
    }

    LOG_I("Camera Parallax: %s, amount=%.3f, delay=%.3f, mouse influence=%.3f",
          out.camera_parallax_enabled ? "enabled" : "disabled", out.camera_parallax_amount, out.camera_parallax_delay,
          out.camera_parallax_mouse_influence);
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

    const cJSON* particle = cJSON_GetObjectItemCaseSensitive(object, "particle");
    if (cJSON_IsString(particle) && particle->valuestring) {
        doc.particle.particle = particle->valuestring;
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
