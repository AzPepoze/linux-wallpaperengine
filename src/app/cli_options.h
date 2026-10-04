#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include <string>
#include <utility>
#include <vector>

#include "shared/graphics/diagnostics/diagnostic_config.h"

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
    bool no_ui = false;
    DiagnosticOptions diagnostics;
    bool cover = false;
    std::string assets_dir;
    int fps_limit = 60;
    std::string scaling;
    std::string clamp;
    std::string screen_root;
    std::string layer;
    std::string layer_size;
    std::string layer_anchor;
    std::string transition;
    int transition_duration_ms = 0;
    bool no_control = false;
    bool particle_debug_bounds = false;
    bool particle_debug_velocity = false;
    float particle_debug_velocity_scale = 0.0f;
    int particle_debug_max_particles = 0;
    std::vector<std::pair<std::string, std::string>> set_properties;

    static CliOptions parse(int argc, char* argv[]);
};

#endif  // CLI_OPTIONS_H
