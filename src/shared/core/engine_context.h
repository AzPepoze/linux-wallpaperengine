#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "shared/assets/asset_manager.h"
#include "shared/graphics/render.h"
#include "sokol_gfx.h"
#include "wallpaper/2d/parser/scene_document.h"
#include "wallpaper/user_properties.h"

typedef enum { SCALING_COVER, SCALING_FIT } scaling_mode_t;
typedef enum { SCENE_TYPE_2D, SCENE_TYPE_3D, SCENE_TYPE_VIDEO, SCENE_TYPE_WEB } scene_type_t;
enum class RuntimeMode { Wallpaper, Sandbox };

class Layer;
class SceneTree;
class ScriptBindings;

struct profiler_stats_t {
    static constexpr size_t HISTORY_SIZE = 128;
    double frame_ms = 0.0;
    double frame_avg_ms = 0.0;
    double frame_peak_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    double ui_ms = 0.0;
    double measured_fps = 0.0;  // real presented rate over a short rolling window
    uint32_t draw_calls = 0;
    uint64_t frame_index = 0;

    float frame_history[HISTORY_SIZE] = {};
    float update_history[HISTORY_SIZE] = {};
    float render_history[HISTORY_SIZE] = {};
    float ui_history[HISTORY_SIZE] = {};
    size_t history_offset = 0;

    double sample_timer = 0.0;
    float sample_interval = 0.040f;  // 40ms per point (~25 Hz) -> ~5.1s continuous visible timeline
};

struct InputState {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float mouse_world_x = 0.0f;
    float mouse_world_y = 0.0f;
    // bit0 left, bit1 right, bit2 middle.
    uint8_t buttons = 0;
    bool mouse_position_valid = false;

    bool left_down() const {
        return (buttons & 0x1u) != 0;
    }
};

struct ParallaxState {
    float pointer_x = 0.5f;
    float pointer_y = 0.5f;
    // Centered shader-space offset. renderer_draw_sprite converts this to
    // g_ParallaxPosition by applying *0.5 + 0.5.
    float smooth_x = 0.0f;
    float smooth_y = 0.0f;
    bool enabled = false;
    float amount = 0.0f;
    float delay = 0.1f;
    float mouse_influence = 0.0f;
};

struct CameraShakeState {
    bool enabled = false;
    float amplitude = 0.0f;
    float speed = 0.0f;
    float roughness = 0.0f;
    // Scene-space camera translation, calculated once per frame.
    float x = 0.0f;
    float y = 0.0f;
};

struct SceneState {
    wallpaper_engine::SceneCameraDocument camera = {};
    wallpaper_engine::SceneGeneralDocument general = {};

    std::vector<Layer*> layers;
    SceneTree* scene_tree = nullptr;
    ScriptBindings* scripts = nullptr;  // property scripts of the scene; deleted before the layers

    float scene_w = 1920.0f;
    float scene_h = 1080.0f;
    float render_scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float perspective_override_fov = 0.0f;
    scaling_mode_t scaling_mode = SCALING_FIT;
};

struct DebugState {
    int selected_object = -1;
    uint32_t selected_node_id = 0;
    bool show_ui = true;
    bool test_mode = false;
    bool show_node_ids = false;
    bool particle_debug_bounds = false;
    bool particle_debug_velocity = false;
    float particle_debug_velocity_scale = 0.05f;
    int particle_debug_max_particles = 128;
};

struct EngineContext {
    sg_pass_action pass_action = {};
    char wallpaper_path[512] = {};
    char engine_path[512] = {};
    char asset_root[512] = {};
    UserProperties user_properties;
    std::vector<std::pair<std::string, std::string>> cli_properties;  // --set-property overrides
    // Web wallpaper renderer options (passed to the out-of-process QtWebEngine helper).
    int web_render_fps = 60;
    bool web_devtools = false;
    int web_devtools_port = 9222;
    bool is_pkg = false;
    RuntimeMode runtime_mode = RuntimeMode::Wallpaper;
    AudioEngine::GroupId audio_group = AudioEngine::kDefaultGroup;

    scene_type_t scene_type = SCENE_TYPE_2D;
    renderer_t renderer = {};
    AssetManager* asset_mgr = nullptr;
    profiler_stats_t profiler = {};

    InputState input;
    SceneState scene;
    ParallaxState parallax;
    CameraShakeState shake;
    DebugState debug;
    float time = 0.0f;
    float frametime = 0.0f;  // seconds since the previous frame (g_Frametime)
};

#endif  // ENGINE_CONTEXT_H
