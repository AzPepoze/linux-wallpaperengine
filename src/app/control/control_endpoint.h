#ifndef CONTROL_ENDPOINT_H
#define CONTROL_ENDPOINT_H

#include <string>

// Filesystem-safe identifier for the control socket of one display/surface.
std::string controlKey(const std::string& screen_root, const std::string& layer);

// Directory that holds all control sockets: $XDG_RUNTIME_DIR/linux-wallpaperengine, or /tmp.
std::string controlSocketDir();

// Full path of the control socket for `key`.
std::string controlSocketPath(const std::string& key);

#endif  // CONTROL_ENDPOINT_H
