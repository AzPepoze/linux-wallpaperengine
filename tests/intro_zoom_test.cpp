#include "wallpaper/2d/camera/intro_zoom.h"

#include <cmath>

#include "test_util.h"

namespace {
bool near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}
}  // namespace

int main() {
    // Starts at the start zoom, ends at 1.0 and holds there.
    CHECK(near(introZoom(1.08f, 4.0f, 0.0f), 1.08f));
    CHECK(near(introZoom(1.08f, 4.0f, 4.0f), 1.0f));
    CHECK(near(introZoom(1.08f, 4.0f, 100.0f), 1.0f));

    CHECK(near(introZoom(1.08f, 4.0f, 2.0f), 1.04f));
    float previous = introZoom(1.08f, 4.0f, 0.0f);
    for (int step = 1; step <= 40; ++step) {
        const float zoom = introZoom(1.08f, 4.0f, 0.1f * (float)step);
        CHECK(zoom <= previous + 1e-6f);
        previous = zoom;
    }
    CHECK(introZoom(1.08f, 4.0f, 0.1f) - 1.08f > -0.001f);  // gentle start

    CHECK(near(introZoom(1.08f, 4.0f, -3.0f), 1.08f));

    CHECK(near(introZoom(1.08f, 0.0f, 0.0f), 1.0f));
    CHECK(near(introZoom(1.08f, -1.0f, 1.0f), 1.0f));
    CHECK(near(introZoom(0.9f, 2.0f, 0.0f), 0.9f));
    CHECK(near(introZoom(1.0f, 4.0f, 1.0f), 1.0f));

    return test::finish("intro zoom tests");
}
