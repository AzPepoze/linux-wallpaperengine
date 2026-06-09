#ifndef CONTEXT_H
#define CONTEXT_H

#include <vector>

#include "../../libs/cJSON.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../asset/asset_manager.h"
#include "../render/render.h"
#include "wallpaper_api.h"

#define MAX_OBJECTS 512

typedef enum { SCALING_COVER, SCALING_FIT } scaling_mode_t;

typedef enum { SCENE_TYPE_2D, SCENE_TYPE_3D, SCENE_TYPE_VIDEO, SCENE_TYPE_WEB } scene_type_t;

#include "../scene/layer.h"

struct app_state_t {
    sg_pass_action pass_action = {};
    char wallpaper_path[512] = {};
    char engine_path[512] = {};
    char asset_root[512] = {};
    bool is_pkg = false;

    scene_type_t scene_type = SCENE_TYPE_2D;
    cJSON* scene_json = nullptr;
    renderer_t renderer = {};
    AssetManager asset_mgr = {};

    std::vector<Layer*> layers;

    float scene_w = 1920.0f;
    float scene_h = 1080.0f;
    float render_scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float parallax_smooth_x = 0.0f;
    float parallax_smooth_y = 0.0f;
    float time = 0.0f;

    scaling_mode_t scaling_mode = SCALING_FIT;
    int selected_object = -1;
    bool show_ui = true;
    bool test_mode = false;
};

#ifdef __cplusplus
extern "C" {
#endif

extern struct app_state_t state;

#ifdef __cplusplus
}
#endif

#endif  // CONTEXT_H
