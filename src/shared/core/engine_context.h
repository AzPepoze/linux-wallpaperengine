#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include <stdint.h>

#include <vector>

#include "shared/assets/asset_manager.h"
#include "shared/graphics/render.h"
#include "sokol_gfx.h"
#include "wallpaper/2d/parser/scene_document.h"
#include "wallpaper_api.h"

typedef enum { SCALING_COVER, SCALING_FIT } scaling_mode_t;
typedef enum { SCENE_TYPE_2D, SCENE_TYPE_3D, SCENE_TYPE_VIDEO, SCENE_TYPE_WEB } scene_type_t;
enum class RuntimeMode { Wallpaper, Sandbox };

class Layer;
class SceneTree;

struct profiler_stats_t {
    double frame_ms = 0.0;
    double frame_avg_ms = 0.0;
    double frame_peak_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    double ui_ms = 0.0;
    uint32_t draw_calls = 0;
    uint64_t frame_index = 0;
};

struct EngineContext {
    sg_pass_action pass_action = {};
    char wallpaper_path[512] = {};
    char engine_path[512] = {};
    char asset_root[512] = {};
    bool is_pkg = false;
    RuntimeMode runtime_mode = RuntimeMode::Wallpaper;

    scene_type_t scene_type = SCENE_TYPE_2D;
    renderer_t renderer = {};
    AssetManager asset_mgr = {};
    profiler_stats_t profiler = {};

    wallpaper_engine::SceneCameraDocument camera = {};
    wallpaper_engine::SceneGeneralDocument general = {};

    std::vector<Layer*> layers;
    SceneTree* scene_tree = nullptr;

    float scene_w = 1920.0f;
    float scene_h = 1080.0f;
    float render_scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool mouse_position_valid = false;
    float parallax_pointer_x = 0.5f;
    float parallax_pointer_y = 0.5f;
    // Centered shader-space offset. renderer_draw_sprite converts this to
    // g_ParallaxPosition by applying *0.5 + 0.5.
    float parallax_smooth_x = 0.0f;
    float parallax_smooth_y = 0.0f;
    bool camera_parallax_enabled = false;
    float camera_parallax_amount = 0.0f;
    float camera_parallax_delay = 0.1f;
    float camera_parallax_mouse_influence = 0.0f;
    bool camera_shake_enabled = false;
    float camera_shake_amplitude = 0.0f;
    float camera_shake_speed = 0.0f;
    float camera_shake_roughness = 0.0f;
    // Scene-space camera translation, calculated once per frame.
    float camera_shake_x = 0.0f;
    float camera_shake_y = 0.0f;
    float perspective_override_fov = 0.0f;
    float time = 0.0f;

    scaling_mode_t scaling_mode = SCALING_FIT;
    int selected_object = -1;
    bool show_ui = true;
    bool test_mode = false;
    bool particle_debug_bounds = false;
    bool particle_debug_velocity = false;
    float particle_debug_velocity_scale = 0.05f;
    int particle_debug_max_particles = 128;
};

#endif  // ENGINE_CONTEXT_H
