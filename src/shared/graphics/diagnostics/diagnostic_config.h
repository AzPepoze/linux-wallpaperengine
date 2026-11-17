#ifndef DIAGNOSTIC_CONFIG_H
#define DIAGNOSTIC_CONFIG_H

#include <stdint.h>

#include <string>
#include <vector>

struct DiagnosticOptions {
    // Set from --capture-dump: every capture shot writes a diagnostics bundle.
    bool enabled = false;
    std::string disable_effects;
    bool disable_particles = false;
    bool disable_bloom = false;
};

struct DiagnosticConfig {
    bool enabled = true;
    std::string output_dir = "./diagnostics";

    int isolate_effect_index = -1;
    std::string isolate_effect_path;
    int isolate_pass_index = -1;
    int stop_after_pass_index = -1;
    int disable_pass_index = -1;
    int force_output_texture_slot = -1;
    int capture_effect_index = -1;
    int capture_pass_index = -1;
    bool capture_pass_images = true;

    // Bisection filters: substring match on the effect path; "*" or "all" matches everything.
    std::vector<std::string> disable_effect_paths;
    bool disable_particles = false;
    bool disable_bloom = false;
};

#endif  // DIAGNOSTIC_CONFIG_H
