// Checks the global pointer source names, relative motion math, and the surface resync rule.

#include "app/platform/pointer/global_pointer.h"

#include <cmath>

#include "test_util.h"
using test::expect;

namespace {

void testSourceNames() {
    PointerSource source = PointerSource::Surface;
    expect("source", parsePointerSource("auto", source) && source == PointerSource::Auto, "auto parses");
    expect("source", parsePointerSource("evdev", source) && source == PointerSource::Evdev, "evdev parses");
    expect("source", parsePointerSource("surface", source) && source == PointerSource::Surface, "surface parses");
    expect("source", !parsePointerSource("hyprland", source), "backends not in the list are rejected");
    expect("source", !parsePointerSource("", source), "empty name is rejected");
}

void testRelativeMotion() {
    OutputPointer start;
    start.x = 0.5f;
    start.y = 0.5f;
    const OutputPointer moved = movedBy(start, 192, -108, 1920.0f, 1080.0f);
    expect("motion", moved.inside, "motion keeps the pointer on the output");
    expect("motion", std::fabs(moved.x - 0.6f) < 1e-5f, "x scales by the output width");
    expect("motion", std::fabs(moved.y - 0.4f) < 1e-5f, "y scales by the output height");

    const OutputPointer far_right = movedBy(start, 100000, 0, 1920.0f, 1080.0f);
    expect("motion", far_right.x == 1.0f, "motion clamps at the right edge");
    const OutputPointer far_up = movedBy(start, 0, -100000, 1920.0f, 1080.0f);
    expect("motion", far_up.y == 0.0f, "motion clamps at the top edge");
}

void testSurfaceOnlyPassthrough() {
    // With the surface as the only source, the position is whatever the engine holds.
    GlobalPointer pointer;
    pointer.open(PointerSource::Surface);

    OutputPointer first;
    first.x = 0.25f;
    first.y = 0.75f;
    first.inside = true;
    const OutputPointer shown = pointer.poll(first, 1920.0f, 1080.0f);
    expect("surface", shown.x == 0.25f && shown.y == 0.75f, "surface position passes through");

    OutputPointer moved = first;
    moved.x = 0.9f;
    const OutputPointer after_event = pointer.poll(moved, 1920.0f, 1080.0f);
    expect("surface", after_event.x == 0.9f, "a later surface position passes through");
}

}  // namespace

int main() {
    testSourceNames();
    testRelativeMotion();
    testSurfaceOnlyPassthrough();
    return test::finish("global pointer tests");
}
