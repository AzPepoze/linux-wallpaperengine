#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include <string>
#include <utility>
#include <vector>

#include "shared/graphics/diagnostics/diagnostic_config.h"
#include "wallpaper/web/web_options.h"

struct TransitionOptions {
    std::string effect;   // --transition
    std::string mode;     // --transition-mode
    int duration_ms = 0;  // --transition-duration
};

// Parallax settings, resolved from config.json. Zero keeps the scene/default value.
struct ParallaxOptions {
    float smoothing = 0.0f;  // parallax_smoothing: response time, overrides the scene
    float scale = 0.0f;      // parallax_scale: particle parallax multiplier
};

struct CliOptions {
    std::string wallpaper_arg;
    bool pkg_flag = false;
    bool sandbox = false;
    bool extract_only = false;
    std::string extract_dir;
    bool has_extract_dir = false;
    std::string gpu;
    bool list_gpus = false;
    bool no_audio = false;
    bool video_ram = false;
    float intro_zoom = 1.0f;
    float intro_duration = 4.0f;
    bool native_effect_resolution = false;
    bool performance_profile = false;
    bool script_profile = false;  // log the most expensive scripts every few seconds
    bool no_ui = false;
    DiagnosticOptions diagnostics;
    bool cover = false;
    std::string assets_dir;
    int fps_limit = 0;  // 0 = unset: rely on vsync, no software cap
    WebOptions web;
    std::string scaling;
    std::string clamp;
    std::string screen_root;
    std::string layer;
    std::string layer_size;
    std::string layer_anchor;
    TransitionOptions transition;
    ParallaxOptions parallax;
    bool no_control = false;
    bool help = false;
    bool whoareyou = false;
    float volume = 100.0f;  // --volume master percent
    bool has_volume = false;
    bool particle_debug_bounds = false;
    bool particle_debug_velocity = false;
    float particle_debug_velocity_scale = 0.0f;
    int particle_debug_max_particles = 0;
    std::vector<std::pair<std::string, std::string>> set_properties;

    std::vector<std::string> startup_options;
    void logResolvedOptions() const;

    static CliOptions parse(int argc, char* argv[]);
};

#endif  // CLI_OPTIONS_H
