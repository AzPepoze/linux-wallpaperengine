#include "app/platform/pointer/exact_pointer.h"

#include <cstdlib>
#include <memory>

#include "app/platform/pointer/hyprland_pointer.h"
#include "app/platform/x11_desktop/x11_desktop.h"

namespace {
bool isX11Session() {
    // Under Wayland, XWayland does not report the global pointer, so an X11 session needs no Wayland display.
    return std::getenv("DISPLAY") != nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr;
}

std::optional<ExactSource> x11Source(const std::string& output) {
    if (!isX11Session() || !x11QueryPointer(output)) return std::nullopt;
    return ExactSource{"x11", [output]() -> std::optional<OutputPointer> {
                           const std::optional<OutputPointer> pointer = x11QueryPointer(output);
                           if (pointer && pointer->inside) return pointer;
                           return std::nullopt;
                       }};
}

std::optional<ExactSource> hyprlandSource(const std::string& output) {
    if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE") == nullptr) return std::nullopt;
    auto hyprland = std::make_shared<HyprlandPointer>();
    if (!hyprland->open(output)) return std::nullopt;
    return ExactSource{"hyprland", [hyprland]() { return hyprland->sample(); }};
}
}  // namespace

std::optional<ExactSource> findExactSource(const std::string& output, PointerSource requested) {
    switch (requested) {
        case PointerSource::Auto: {
            if (auto x11 = x11Source(output)) return x11;
            return hyprlandSource(output);
        }
        case PointerSource::X11:
            return x11Source(output);
        case PointerSource::Hyprland:
            return hyprlandSource(output);
        case PointerSource::Surface:
        case PointerSource::Evdev:
            return std::nullopt;
    }
    return std::nullopt;
}
