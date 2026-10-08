#ifndef EXACT_POINTER_H
#define EXACT_POINTER_H

#include <functional>
#include <optional>
#include <string>

#include "app/platform/pointer/output_pointer.h"
#include "app/platform/pointer/pointer_source.h"

// A desktop source that reports the real cursor position. `sample` returns nothing when the cursor is off the output.
struct ExactSource {
    std::string name;
    std::function<std::optional<OutputPointer>()> sample;
};

// The exact source the session offers: X11 first, then Hyprland. `requested` narrows the choice; nullopt when none works.
std::optional<ExactSource> findExactSource(const std::string& output, PointerSource requested);

#endif  // EXACT_POINTER_H
