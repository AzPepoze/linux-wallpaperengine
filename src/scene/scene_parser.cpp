#include "scene_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/config.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "2d/image_layer.h"
#include "2d/particle_layer.h"

namespace {

const cJSON* property_value(const cJSON* node) {
    if (cJSON_IsObject(node)) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(node, "value");
        if (value) return value;
    }
    return node;
}

bool parse_vec(const cJSON* raw, float* out, int count) {
    const cJSON* node = property_value(raw);
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

bool parse_bool(const cJSON* raw, bool fallback = false) {
    const cJSON* node = property_value(raw);
    if (cJSON_IsBool(node)) return cJSON_IsTrue(node);
    if (cJSON_IsNumber(node)) return node->valuedouble != 0.0;
    return fallback;
}

bool parse_parallax_node(cJSON* object, scene_parallax_node_t& out) {
    cJSON* id = cJSON_GetObjectItemCaseSensitive(object, "id");
    if (!cJSON_IsNumber(id) || id->valuedouble <= 0.0) return false;

    out.id = (uint32_t)id->valuedouble;

    cJSON* parent = cJSON_GetObjectItemCaseSensitive(object, "parent");
    if (cJSON_IsNumber(parent) && parent->valuedouble > 0.0) {
        out.parent_id = (uint32_t)parent->valuedouble;
    }

    parse_vec(cJSON_GetObjectItemCaseSensitive(object, "origin"), out.origin, 3);
    parse_vec(cJSON_GetObjectItemCaseSensitive(object, "scale"), out.scale, 3);
    parse_vec(cJSON_GetObjectItemCaseSensitive(object, "angles"), out.angles, 3);

    const bool has_depth =
        parse_vec(cJSON_GetObjectItemCaseSensitive(object, "parallaxDepth"), out.depth, 2);

    // Wallpaper Engine gives compose-layer containers regular layer depth when
    // parallaxDepth is omitted. Match ImageObject::FromJson in OWE.
    if (!has_depth) {
        cJSON* image = cJSON_GetObjectItemCaseSensitive(object, "image");
        if (cJSON_IsString(image) && image->valuestring &&
            strcmp(image->valuestring, "models/util/composelayer.json") == 0) {
            out.depth[0] = 1.0f;
            out.depth[1] = 1.0f;
        }
    }

    const bool disable_propagation =
        parse_bool(cJSON_GetObjectItemCaseSensitive(object, "disablepropagation"), false);
    out.propagate_to_children = !disable_propagation;
    return true;
}

}  // namespace

ParsedScene SceneParser::parse(const char* scene_json_path, EngineContext& ctx) {
    ParsedScene out;

    char* json_str = read_file_to_string(scene_json_path);
    if (!json_str) {
        LOG_E("Failed to read scene JSON: %s", scene_json_path);
        return out;
    }

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        LOG_E("Failed to parse scene JSON");
        return out;
    }

    LOG_I("Scene JSON parsed successfully");

    detectResolution(root, out);
    // Layer constructors (notably particles) need the authored orthographic
    // extent while the scene is being parsed, not only after parse() returns.
    ctx.scene_w = out.design_width;
    ctx.scene_h = out.design_height;

    cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    if (general) {
        parseGeneral(general, out);
    }

    cJSON* objects = cJSON_GetObjectItemCaseSensitive(root, "objects");
    if (cJSON_IsArray(objects)) {
        // First pass: preserve every object that can participate in transform/
        // parallax inheritance, including non-rendering container objects.
        cJSON* obj_json;
        cJSON_ArrayForEach(obj_json, objects) {
            scene_parallax_node_t node;
            if (parse_parallax_node(obj_json, node)) out.parallax_nodes.push_back(node);
        }

        // Second pass: instantiate renderable layer types supported by this renderer.
        cJSON_ArrayForEach(obj_json, objects) {
            Layer* layer = createLayer(obj_json, ctx);
            if (layer) {
                out.layers.push_back(layer);
            }
        }
    }

    LOG_I("Parsed %zu scene transform/parallax nodes", out.parallax_nodes.size());

    cJSON_Delete(root);
    return out;
}

void SceneParser::detectResolution(cJSON* root, ParsedScene& out) {
    out.design_width = 0;
    out.design_height = 0;

    // 1. Check root resolution
    cJSON* resolution = cJSON_GetObjectItemCaseSensitive(root, "resolution");
    if (cJSON_IsString(resolution)) {
        sscanf(resolution->valuestring, "%f %f", &out.design_width, &out.design_height);
    }

    // 2. Check general -> orthogonalprojection (Common in 4K scenes)
    cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    if (out.design_width == 0 && general) {
        cJSON* ortho = cJSON_GetObjectItemCaseSensitive(general, "orthogonalprojection");
        if (ortho) {
            cJSON* w = cJSON_GetObjectItemCaseSensitive(ortho, "width");
            cJSON* h = cJSON_GetObjectItemCaseSensitive(ortho, "height");
            if (cJSON_IsNumber(w)) out.design_width = (float)w->valuedouble;
            if (cJSON_IsNumber(h)) out.design_height = (float)h->valuedouble;
        }
    }

    // 3. Fallback to default
    if (out.design_width == 0) {
        out.design_width = Config::kDefaultSceneWidth;
        out.design_height = Config::kDefaultSceneHeight;
        LOG_W("Design resolution not found, defaulting to %dx%d", (int)out.design_width, (int)out.design_height);
    } else {
        LOG_I("Detected Design Resolution: %.0fx%.0f", out.design_width, out.design_height);
    }
}

void SceneParser::parseGeneral(cJSON* general, ParsedScene& out) {
    cJSON* cc = cJSON_GetObjectItemCaseSensitive(general, "clearcolor");
    if (cJSON_IsString(cc)) {
        float r, g, b;
        if (sscanf(cc->valuestring, "%f %f %f", &r, &g, &b) == 3) {
            out.clear_color[0] = r;
            out.clear_color[1] = g;
            out.clear_color[2] = b;
            out.clear_color[3] = 1.0f;
            out.has_clear_color = true;
        }
    }

    cJSON* camera_parallax = cJSON_GetObjectItemCaseSensitive(general, "cameraparallax");
    if (cJSON_IsBool(camera_parallax)) {
        out.camera_parallax_enabled = cJSON_IsTrue(camera_parallax);
    } else if (cJSON_IsObject(camera_parallax)) {
        cJSON* value = cJSON_GetObjectItemCaseSensitive(camera_parallax, "value");
        if (cJSON_IsBool(value)) out.camera_parallax_enabled = cJSON_IsTrue(value);
    }

    cJSON* amount = cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxamount");
    if (cJSON_IsNumber(amount)) out.camera_parallax_amount = (float)amount->valuedouble;

    cJSON* delay = cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxdelay");
    if (cJSON_IsNumber(delay)) out.camera_parallax_delay = (float)delay->valuedouble;

    cJSON* mouse_influence = cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxmouseinfluence");
    if (cJSON_IsNumber(mouse_influence)) {
        out.camera_parallax_mouse_influence = (float)mouse_influence->valuedouble;
    }

    LOG_I("Camera Parallax: %s, amount=%.3f, delay=%.3f, mouse influence=%.3f",
          out.camera_parallax_enabled ? "enabled" : "disabled", out.camera_parallax_amount,
          out.camera_parallax_delay, out.camera_parallax_mouse_influence);
}

Layer* SceneParser::createLayer(cJSON* obj_json, EngineContext& ctx) {
    Layer* layer = ParticleLayer::createFromJSON(obj_json, ctx);
    if (!layer) {
        layer = ImageLayer::createFromJSON(obj_json, ctx);
    }
    return layer;
}