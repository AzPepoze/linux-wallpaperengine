#ifndef GLOBAL_POINTER_H
#define GLOBAL_POINTER_H

#include <functional>
#include <optional>
#include <string>

#include "app/platform/pointer/evdev_pointer.h"
#include "app/platform/pointer/output_pointer.h"
#include "app/platform/pointer/pointer_source.h"

// Finds the pointer position on this output. Sources, most accurate first: an exact desktop source (X11 or
// Hyprland), the wallpaper surface while the pointer is over it, then mouse motion from evdev as an estimate.
class GlobalPointer {
   public:
    void open(const std::string& output, PointerSource source);
    // `over_surface` is true while the pointer is over the wallpaper, so the surface's own position is exact.
    OutputPointer poll(OutputPointer current, bool over_surface, float width, float height);
    // Replaces the exact desktop source. Tests use this to drive the exact path without a desktop.
    void setExactSampler(std::function<std::optional<OutputPointer>()> sampler);
    // Which source gave the last position: x11, hyprland, surface, evdev or none.
    const std::string& source() const { return source_; }

   private:
    OutputPointer remember(OutputPointer position, const std::string& name);

    std::function<std::optional<OutputPointer>()> exact_;
    std::string exact_name_ = "exact";
    std::string source_ = "none";
    bool use_surface_ = true;
    bool use_evdev_ = false;
    EvdevPointer evdev_;
    OutputPointer last_;
    bool has_last_ = false;
};

#endif  // GLOBAL_POINTER_H
