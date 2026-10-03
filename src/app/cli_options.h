#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include <string>

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
    bool no_ui = false;
    bool diagnose = false;
    bool cover = false;
    bool particle_debug_bounds = false;
    bool particle_debug_velocity = false;
    float particle_debug_velocity_scale = 0.0f;
    int particle_debug_max_particles = 0;

    static CliOptions parse(int argc, char* argv[]);
};

#endif  // CLI_OPTIONS_H
