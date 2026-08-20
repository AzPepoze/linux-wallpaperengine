#ifndef SCENE_BUILDER_H
#define SCENE_BUILDER_H

#include <vector>

#include "core/engine_context.h"
#include "formats/wallpaper_engine/scene/scene_document.h"
#include "wallpaper/scene/2d/layers/layer.h"
#include "wallpaper/scene/graph/scene_graph.h"

struct ParsedScene {
    std::vector<Layer*> layers;
    SceneGraph* scene_graph = nullptr;
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

class SceneBuilder {
   public:
    static ParsedScene buildFromDocument(const wallpaper_engine::SceneDocument& document, EngineContext& ctx);
    static ParsedScene load(const char* scene_json_path, EngineContext& ctx);
};

#endif  // SCENE_BUILDER_H
