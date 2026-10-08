#ifndef POINTER_SOURCE_H
#define POINTER_SOURCE_H

#include <string>

// Where the global pointer position comes from; Auto uses evdev when it can, else the surface only.
enum class PointerSource { Auto, Surface, Evdev };

// Accepts "auto", "surface" or "evdev"; false for anything else.
bool parsePointerSource(const std::string& text, PointerSource& out);

#endif  // POINTER_SOURCE_H
