#include "app/frame_rate.h"

#include "test_util.h"

int main() {
    // policyFor: <=0 means "unset" -> rely on vsync, no software cap.
    frame_rate::Policy p = frame_rate::policyFor(0);
    CHECK(p.software_limit == 0);
    CHECK(p.vsync);
    p = frame_rate::policyFor(-1);
    CHECK(p.software_limit == 0);
    CHECK(p.vsync);
    p = frame_rate::policyFor(30);
    CHECK(p.software_limit == 30);
    CHECK(!p.vsync);
    p = frame_rate::policyFor(144);
    CHECK(p.software_limit == 144);
    CHECK(!p.vsync);

    // Meter reports 0 until it has accumulated a window.
    frame_rate::Meter meter;
    CHECK(meter.fps() == 0.0);

    // 60 frames at 1/60 s -> ~60 fps.
    for (int i = 0; i < 60; ++i) meter.tick(1.0 / 60.0);
    CHECK(meter.fps() > 59.0);
    CHECK(meter.fps() < 61.0);

    // 90 frames at 1/30 s -> ~30 fps.
    frame_rate::Meter slow;
    for (int i = 0; i < 90; ++i) slow.tick(1.0 / 30.0);
    CHECK(slow.fps() > 29.0);
    CHECK(slow.fps() < 31.0);

    // Non-positive dt is ignored (no divide-by-zero, no frame counted).
    frame_rate::Meter guard;
    guard.tick(0.0);
    guard.tick(-1.0);
    CHECK(guard.fps() == 0.0);

    return test::finish("frame rate checks");
}
