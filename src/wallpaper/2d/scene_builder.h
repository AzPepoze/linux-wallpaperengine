#ifndef SCENE_BUILDER_H
#define SCENE_BUILDER_H

#include <vector>

#include "shared/core/engine_context.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/parser/scene_document.h"
#include "wallpaper/2d/tree/scene_tree.h"

struct ParsedScene {
    std::vector<Layer*> layers;
    SceneTree* scene_tree = nullptr;
    wallpaper_engine::SceneCameraDocument camera;
    wallpaper_engine::SceneGeneralDocument general;
    float design_width = 0.0f;
    float design_height = 0.0f;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear_color = false;
    bool camera_parallax_enabled = false;
    float camera_parallax_amount = 0.0f;
    float camera_parallax_delay = 0.1f;
    float camera_parallax_mouse_influence = 0.0f;
    bool camera_shake_enabled = false;
    float camera_shake_amplitude = 0.0f;
    float camera_shake_speed = 0.0f;
    float camera_shake_roughness = 0.0f;
    scene_type_t type = SCENE_TYPE_2D;
};

class SceneBuilder {
   public:
    static ParsedScene buildFromDocument(const wallpaper_engine::SceneDocument& document, EngineContext& ctx);
    static ParsedScene buildVideoScene(const char* video_path, EngineContext& ctx);
    static ParsedScene load(const char* scene_json_path, EngineContext& ctx);
};

#endif  // SCENE_BUILDER_H
