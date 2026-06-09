#include "scene_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/config.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "2d/image_layer.h"
#include "2d/particle_layer.h"

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

    cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    if (general) {
        parseGeneral(general, out);
    }

    cJSON* objects = cJSON_GetObjectItemCaseSensitive(root, "objects");
    if (cJSON_IsArray(objects)) {
        cJSON* obj_json;
        cJSON_ArrayForEach(obj_json, objects) {
            Layer* layer = createLayer(obj_json, ctx);
            if (layer) {
                out.layers.push_back(layer);
            }
        }
    }

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
}

Layer* SceneParser::createLayer(cJSON* obj_json, EngineContext& ctx) {
    Layer* layer = ParticleLayer::createFromJSON(obj_json, ctx);
    if (!layer) {
        layer = ImageLayer::createFromJSON(obj_json, ctx);
    }
    return layer;
}
