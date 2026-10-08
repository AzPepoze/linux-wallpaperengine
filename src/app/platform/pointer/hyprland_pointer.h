#ifndef HYPRLAND_POINTER_H
#define HYPRLAND_POINTER_H

#include <optional>
#include <string>

#include "app/platform/pointer/output_pointer.h"

struct HyprlandMonitor {
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// Parses the reply of "j/cursorpos": {"x": .., "y": ..} in global layout coordinates.
bool parseHyprlandCursor(const std::string& json, double& x, double& y);

// Finds the monitor called `name` in the reply of "j/monitors".
bool parseHyprlandMonitor(const std::string& json, const std::string& name, HyprlandMonitor& out);

// Maps a global point onto a monitor; `inside` is false when the point is off that monitor.
OutputPointer pointOnMonitor(double x, double y, const HyprlandMonitor& monitor);

// Reads the exact global pointer from Hyprland's IPC socket. Only usable inside a Hyprland session.
class HyprlandPointer {
   public:
    // False when Hyprland's socket or the named monitor is not available.
    bool open(const std::string& output);
    std::optional<OutputPointer> sample();

   private:
    std::string output_;
};

#endif  // HYPRLAND_POINTER_H
