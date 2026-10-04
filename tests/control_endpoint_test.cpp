#include <cstdlib>
#include <string>

#include "app/control/control_endpoint.h"
#include "test_util.h"

int main() {
    CHECK(controlKey("DP-4", "") == "DP-4");
    CHECK(controlKey("", "") == "window");
    CHECK(controlKey("", "bottom") == "window-bottom");
    CHECK(controlKey("HDMI-A-1", "overlay") == "HDMI-A-1-overlay");
    // Slashes and spaces must not survive into a filename.
    const std::string key = controlKey("a/b c", "top");
    CHECK(key.find('/') == std::string::npos);
    CHECK(key.find(' ') == std::string::npos);

    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    CHECK(controlSocketPath("DP-4") == "/run/user/1000/linux-wallpaperengine/DP-4.sock");
    unsetenv("XDG_RUNTIME_DIR");
    CHECK(controlSocketPath("window") == "/tmp/linux-wallpaperengine/window.sock");
    return test::finish("control endpoint checks");
}
