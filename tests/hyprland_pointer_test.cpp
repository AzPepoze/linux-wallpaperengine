// Checks how Hyprland's replies are read: the cursor position and the monitor it is mapped onto.

#include "app/platform/pointer/hyprland_pointer.h"

#include <cmath>

#include "test_util.h"
using test::expect;

namespace {

const char* kMonitors =
    R"([{"name": "DP-4", "x": 0, "y": 0, "width": 1920, "height": 1080},)"
    R"( {"name": "HDMI-A-1", "x": 1920, "y": 0, "width": 2560, "height": 1440}])";

void testCursorReply() {
    double x = 0.0, y = 0.0;
    expect("cursor", parseHyprlandCursor(R"({"x": 502, "y": 867})", x, y), "a cursor reply parses");
    expect("cursor", x == 502.0 && y == 867.0, "the cursor position is read");
    expect("cursor", !parseHyprlandCursor("not json", x, y), "a broken reply is rejected");
}

void testMonitorLookup() {
    HyprlandMonitor monitor;
    expect("monitor", parseHyprlandMonitor(kMonitors, "HDMI-A-1", monitor), "a named monitor is found");
    expect("monitor", monitor.x == 1920.0 && monitor.width == 2560.0, "its position and size are read");
    expect("monitor", !parseHyprlandMonitor(kMonitors, "DP-9", monitor), "an unknown monitor is not found");
}

void testPointOnMonitor() {
    HyprlandMonitor dp4;
    dp4.x = 0.0;
    dp4.y = 0.0;
    dp4.width = 1920.0;
    dp4.height = 1080.0;

    const OutputPointer centre = pointOnMonitor(960.0, 540.0, dp4);
    expect("map", centre.inside && std::fabs(centre.x - 0.5f) < 1e-5f && std::fabs(centre.y - 0.5f) < 1e-5f,
           "the centre of the monitor maps to 0.5, 0.5");

    const OutputPointer other_monitor = pointOnMonitor(2000.0, 300.0, dp4);
    expect("map", !other_monitor.inside, "a point on another monitor is not inside this one");
}

}  // namespace

int main() {
    testCursorReply();
    testMonitorLookup();
    testPointOnMonitor();
    return test::finish("hyprland pointer tests");
}
