#ifndef DIAGNOSTIC_CONFIG_H
#define DIAGNOSTIC_CONFIG_H

#include <stdint.h>

#include <string>

struct DiagnosticConfig {
    bool enabled = false;
    std::string output_dir = "./diagnostics";
    uint64_t target_frame = 120;
    bool has_deterministic_time = false;
    float deterministic_time = 0.0f;

    // Pass isolation controls
    int isolate_effect_index = -1;
    std::string isolate_effect_path;
    int isolate_pass_index = -1;
    int stop_after_pass_index = -1;
    int disable_pass_index = -1;
    int force_output_texture_slot = -1;

    // A/B test flag (deferred / controlled)
    bool enable_ab = false;

    // Runtime state flags
    bool capture_triggered = false;
    bool capture_complete = false;

    void parseFromArgs();
    bool shouldCaptureFrame(uint64_t frame_index) const {
        return enabled && !capture_complete && (frame_index == target_frame);
    }
};

#endif  // DIAGNOSTIC_CONFIG_H
