#include "app/control/control_endpoint.h"

#include <cstdlib>

namespace {
bool isSafe(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
}
}  // namespace

std::string controlKey(const std::string& screen_root, const std::string& layer) {
    std::string key = screen_root.empty() ? "window" : screen_root;
    if (!layer.empty()) key += "-" + layer;
    for (char& c : key) {
        if (!isSafe(c)) c = '-';
    }
    return key;
}

std::string controlSocketDir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    const std::string base = (xdg && xdg[0]) ? xdg : "/tmp";
    return base + "/linux-wallpaperengine";
}

std::string controlSocketPath(const std::string& key) {
    return controlSocketDir() + "/" + key + ".sock";
}
