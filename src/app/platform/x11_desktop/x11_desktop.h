#ifndef X11_DESKTOP_H
#define X11_DESKTOP_H

#include <optional>
#include <string>

#include "app/platform/pointer/output_pointer.h"

// Window title that identifies this process's window on one output; sokol names the window with it.
std::string x11DesktopTitle(const std::string& output);

// Turns the window with `title` into a desktop window covering the named RandR output
// (the primary output when the name is not found). False when the window or an output is missing.
bool x11PlaceDesktopWindow(const std::string& title, const std::string& output);

// The real pointer position on the named output, or nothing when the X11 feature is not available.
std::optional<OutputPointer> x11QueryPointer(const std::string& output);

#endif  // X11_DESKTOP_H
