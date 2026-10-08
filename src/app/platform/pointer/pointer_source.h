#ifndef POINTER_SOURCE_H
#define POINTER_SOURCE_H

#include <string>

// Where the pointer position comes from. Auto tries every source, most accurate first.
enum class PointerSource { Auto, Surface, X11, Hyprland, Evdev };

// Accepts "auto", "surface", "x11", "hyprland" or "evdev"; false for anything else.
bool parsePointerSource(const std::string& text, PointerSource& out);

#endif  // POINTER_SOURCE_H
