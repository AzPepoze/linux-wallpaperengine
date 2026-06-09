#ifndef SCENE_PARSER_H
#define SCENE_PARSER_H

#include <vector>
#include <memory>
#include "../core/engine_context.h"
#include "layer.h"

struct ParsedScene {
    std::vector<Layer*> layers; // Using raw pointers as Layer doesn't support smart pointers well yet
    float design_width = 0.0f;
    float design_height = 0.0f;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear_color = false;
    scene_type_t type = SCENE_TYPE_2D;
};

class SceneParser {
public:
    static ParsedScene parse(const char* scene_json_path, EngineContext& ctx);
private:
    static void parseGeneral(cJSON* general, ParsedScene& out);
    static Layer* createLayer(cJSON* obj_json, EngineContext& ctx);
    static void detectResolution(cJSON* root, ParsedScene& out);
};

#endif // SCENE_PARSER_H
