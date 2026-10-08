// Checks the pointer source names, the relative motion math, and the order the sources are used in.

#include "app/platform/pointer/global_pointer.h"

#include <cmath>
#include <optional>

#include "test_util.h"
using test::expect;

namespace {

OutputPointer at(float x, float y, bool inside = true) {
    OutputPointer pointer;
    pointer.x = x;
    pointer.y = y;
    pointer.inside = inside;
    return pointer;
}

void testSourceNames() {
    PointerSource source = PointerSource::Surface;
    expect("source", parsePointerSource("auto", source) && source == PointerSource::Auto, "auto parses");
    expect("source", parsePointerSource("x11", source) && source == PointerSource::X11, "x11 parses");
    expect("source", parsePointerSource("hyprland", source) && source == PointerSource::Hyprland, "hyprland parses");
    expect("source", parsePointerSource("evdev", source) && source == PointerSource::Evdev, "evdev parses");
    expect("source", parsePointerSource("surface", source) && source == PointerSource::Surface, "surface parses");
    expect("source", !parsePointerSource("wayland", source), "unknown names are rejected");
    expect("source", !parsePointerSource("", source), "empty name is rejected");
}

void testRelativeMotion() {
    const OutputPointer moved = movedBy(at(0.5f, 0.5f), 192, -108, 1920.0f, 1080.0f);
    expect("motion", moved.inside, "motion keeps the pointer on the output");
    expect("motion", std::fabs(moved.x - 0.6f) < 1e-5f, "x scales by the output width");
    expect("motion", std::fabs(moved.y - 0.4f) < 1e-5f, "y scales by the output height");

    expect("motion", movedBy(at(0.5f, 0.5f), 100000, 0, 1920.0f, 1080.0f).x == 1.0f, "motion clamps at the right edge");
    expect("motion", movedBy(at(0.5f, 0.5f), 0, -100000, 1920.0f, 1080.0f).y == 0.0f, "motion clamps at the top edge");
}

void testExactSourceWinsOverEverything() {
    GlobalPointer pointer;
    pointer.setExactSampler([] { return std::optional<OutputPointer>(at(0.2f, 0.3f)); });
    const OutputPointer shown = pointer.poll(at(0.9f, 0.9f), true, 1920.0f, 1080.0f);
    expect("order", shown.x == 0.2f && shown.y == 0.3f, "an exact source wins over the surface while hovering");
    const OutputPointer off_surface = pointer.poll(at(0.9f, 0.9f), false, 1920.0f, 1080.0f);
    expect("order", off_surface.x == 0.2f, "an exact source wins when the pointer is off the surface");
}

void testSurfaceUsedWhenNoExactSource() {
    GlobalPointer pointer;
    pointer.setExactSampler([] { return std::optional<OutputPointer>(); });
    const OutputPointer hovering = pointer.poll(at(0.25f, 0.75f), true, 1920.0f, 1080.0f);
    expect("order", hovering.x == 0.25f && hovering.y == 0.75f, "the surface position is used while hovering");
}

void testSurfaceOnlyWithoutHover() {
    // No exact source and no evdev: the engine keeps its last position.
    GlobalPointer pointer;
    pointer.setExactSampler([] { return std::optional<OutputPointer>(); });
    const OutputPointer kept = pointer.poll(at(0.4f, 0.6f), false, 1920.0f, 1080.0f);
    expect("order", kept.x == 0.4f && kept.y == 0.6f, "without hover or exact data the last position is kept");
}

void testExactOutsideOutputFallsThrough() {
    GlobalPointer pointer;
    pointer.setExactSampler([] { return std::optional<OutputPointer>(at(1.5f, 0.5f, false)); });
    const OutputPointer shown = pointer.poll(at(0.3f, 0.3f), true, 1920.0f, 1080.0f);
    expect("order", shown.x == 0.3f, "an exact position off the output falls through to the surface");
}

}  // namespace

int main() {
    testSourceNames();
    testRelativeMotion();
    testExactSourceWinsOverEverything();
    testSurfaceUsedWhenNoExactSource();
    testSurfaceOnlyWithoutHover();
    testExactOutsideOutputFallsThrough();
    return test::finish("global pointer tests");
}
