#ifndef DIAGNOSTIC_CONFIG_H
#define DIAGNOSTIC_CONFIG_H

#include <stdint.h>

#include <string>
#include <vector>

struct DiagnosticOptions {
    bool enabled = false;
    std::string disable_effects;
    bool disable_particles = false;
    bool disable_bloom = false;
    bool final_only = false;
    bool exit_after_diagnose = false;
    // Fixed 1/60 s frame step and a seeded random generator, so a capture frame is identical from run to run.
    bool deterministic = false;
    // Frame to capture; a hidden window needs a low value since frames advance only on compositor request.
    int target_frame = 100;
};

struct DiagnosticConfig {
    bool enabled = true;
    std::string output_dir = "./diagnostics";
    uint64_t target_frame = 100;
    bool has_deterministic_time = false;
    float deterministic_time = 0.0f;
    bool fixed_step = false;

    int isolate_effect_index = -1;
    std::string isolate_effect_path;
    int isolate_pass_index = -1;
    int stop_after_pass_index = -1;
    int disable_pass_index = -1;
    int force_output_texture_slot = -1;
    int capture_effect_index = -1;
    int capture_pass_index = -1;
    bool capture_pass_images = true;

    bool final_only = false;

    bool exit_after_diagnose = false;

    // Bisection filters: substring match on the effect path; "*" or "all" matches everything.
    std::vector<std::string> disable_effect_paths;
    bool disable_particles = false;
    bool disable_bloom = false;

    bool enable_ab = false;

    bool capture_triggered = false;
    bool capture_complete = false;

    bool shouldCaptureFrame(uint64_t frame_index) const {
        return enabled && !capture_complete && (frame_index == target_frame);
    }
};

#endif  // DIAGNOSTIC_CONFIG_H
