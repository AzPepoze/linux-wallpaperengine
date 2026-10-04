#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

#include <string>
#include <utility>
#include <vector>

#include "wallpaper/transition/transition_catalog.h"

// One wallpaper-switch command sent from a second launch to the live instance.
struct SwitchRequest {
    std::string path;
    bool is_pkg = false;
    std::vector<std::pair<std::string, std::string>> properties;
    int transition = lwe::transition::kSelectionNone;
    int transition_time_ms = 1000;
};

// Newline-free JSON. decode leaves `out` untouched on failure and fills `error`.
std::string encodeSwitchRequest(const SwitchRequest& request);
bool decodeSwitchRequest(const std::string& json, SwitchRequest& out, std::string& error);

std::string encodeReply(bool ok, const std::string& error = {});

#endif  // CONTROL_PROTOCOL_H
