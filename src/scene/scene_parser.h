#ifndef SCENE_PARSER_H
#define SCENE_PARSER_H

#include <vector>

#include "../core/engine_context.h"
#include "layer.h"

struct ParsedScene {
    std::vector<Layer*> layers;  // Transitional raw ownership; SceneRuntime owns these after parsing.
    std::vector<scene_parallax_node_t> parallax_nodes;
    float design_width = 0.0f;
    float design_height = 0.0f;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear_color = false;
    bool camera_parallax_enabled = false;
    float camera_parallax_amount = 0.0f;
    float camera_parallax_delay = 0.1f;
    float camera_parallax_mouse_influence = 0.0f;
    scene_type_t type = SCENE_TYPE_2D;
};

// Runtime adapter. Wallpaper Engine JSON parsing lives in
// formats/wallpaper_engine/scene; this class turns the typed document into the
// legacy runtime objects until the SceneGraph/layer migration is complete.
class SceneParser {
   public:
    static ParsedScene parse(const char* scene_json_path, EngineContext& ctx);

   private:
    static Layer* createLayer(cJSON* obj_json, EngineContext& ctx);
};

#endif  // SCENE_PARSER_H
