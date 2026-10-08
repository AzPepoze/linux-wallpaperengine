#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

#include <string>
#include <utility>
#include <vector>

#include "wallpaper/transition/transition_catalog.h"

struct SwitchRequest {
    std::string path;
    bool is_pkg = false;
    std::vector<std::pair<std::string, std::string>> properties;
    int transition = lwe::transition::kSelectionNone;
    int transition_time_ms = 1000;
    bool continue_previous = false;

    // Absent fields keep the running instance's state.
    std::string scaling;  // "default"|"fill"|"fit"|"stretch"
    float volume = 0.0f;  // master volume, 0-100 (CLI units)
    bool has_volume = false;
    bool muted = false;
    bool has_muted = false;
    int fps = 0;
    bool has_fps = false;

    // Flips the debug panel of the running wallpaper; no switch happens.
    bool toggle_debug_ui = false;
};

// Newline-free JSON. decode leaves `out` untouched on failure and fills `error`.
std::string encodeSwitchRequest(const SwitchRequest& request);
bool decodeSwitchRequest(const std::string& json, SwitchRequest& out, std::string& error);

std::string encodeReply(bool ok, const std::string& error = {});

#endif  // CONTROL_PROTOCOL_H
