#ifndef EXACT_POINTER_H
#define EXACT_POINTER_H

#include <functional>
#include <optional>
#include <string>

#include "app/platform/pointer/output_pointer.h"
#include "app/platform/pointer/pointer_source.h"

// Reports the real cursor position; `sample` is empty when the cursor is off the output.
struct ExactSource {
    std::string name;
    std::function<std::optional<OutputPointer>()> sample;
};

// Chooses X11 before Hyprland; `requested` narrows the choice, and nullopt means neither works.
std::optional<ExactSource> findExactSource(const std::string& output, PointerSource requested);

#endif  // EXACT_POINTER_H
