#ifndef WEB_OPTIONS_H
#define WEB_OPTIONS_H

#include <string>

#include "wallpaper/web/web_transport.h"

// Web renderer settings, resolved from the CLI and config.json.
struct WebOptions {
    WebTransport transport = WebTransport::Auto;
    bool devtools = true;
    int devtools_port = 9222;
    std::string devtools_browser;
    int render_fps = 60;  // derived from the frame-rate policy, not a flag
};

#endif  // WEB_OPTIONS_H
