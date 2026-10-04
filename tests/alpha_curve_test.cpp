#include "wallpaper/2d/alpha_curve.h"

#include <math.h>
#include <stdio.h>

#include "test_util.h"
#include "wallpaper/2d/animation_curve.h"
using test::check;

using wallpaper_engine::ImageObjectDocument;

namespace {
bool near(float a, float b, float eps = 1e-3f) {
    return fabsf(a - b) < eps;
}

ImageObjectDocument makeDocument(const char* mode) {
    ImageObjectDocument image;
    image.alpha = 1.0f;
    image.alpha_fps = 30.0f;
    image.alpha_length = 90.0f;
    image.alpha_mode = mode;
    image.alpha_keys = {{0.0f, 1.0f}, {60.0f, 1.0f}, {90.0f, 0.0f}};
    return image;
}
}  // namespace

int main() {
    // "single" plays once and holds the final keyframe value afterwards.
    {
        const ImageObjectDocument image = makeDocument("single");
        check(near(evaluateImageAlpha(image, 0.0f), 1.0f), "single: starts at first key");
        check(near(evaluateImageAlpha(image, 1.5f), 1.0f), "single: holds while flat");
        check(near(evaluateImageAlpha(image, 2.5f), 0.5f), "single: interpolates the fade");
        check(near(evaluateImageAlpha(image, 3.0f), 0.0f), "single: reaches final key at length");
        check(near(evaluateImageAlpha(image, 5.0f), 0.0f), "single: holds final value after length");
        check(near(evaluateImageAlpha(image, 30.0f), 0.0f), "single: never wraps back to the start");
    }

    // "loop" keeps wrapping.
    {
        const ImageObjectDocument image = makeDocument("loop");
        check(near(evaluateImageAlpha(image, 5.0f), 1.0f), "loop: wraps back to frame 60");
    }

    // "mirror" ping-pongs.
    {
        const ImageObjectDocument image = makeDocument("mirror");
        check(near(evaluateImageAlpha(image, 3.0f), 0.0f), "mirror: reaches the end at length");
        check(near(evaluateImageAlpha(image, 5.0f), 1.0f), "mirror: reflects back toward the start");
    }

    // Missing mode keeps the historical looping behaviour.
    {
        const ImageObjectDocument image = makeDocument("");
        check(near(evaluateImageAlpha(image, 5.0f), 1.0f), "empty mode: defaults to loop");
    }

    // Effect constants (e.g. a scalar opacity) use the same curve semantics.
    {
        const std::vector<wallpaper_engine::CurveKeyframe> keys = {{0.0f, 0.75f}, {160.0f, 1.0f}};
        check(near(evaluateCurve(keys, 30.0f, 300.0f, "loop", 0.0f), 0.75f), "curve loop: starts at first key");
        check(near(evaluateCurve(keys, 30.0f, 300.0f, "loop", 160.0f / 30.0f), 1.0f), "curve loop: reaches last key");
        check(near(evaluateCurve(keys, 30.0f, 300.0f, "loop", 300.0f / 30.0f), 0.75f), "curve loop: wraps to start");
        check(near(evaluateCurve(keys, 30.0f, 300.0f, "single", 15.0f), 1.0f), "curve single: holds last key");
        check(near(evaluateCurve({}, 30.0f, 300.0f, "loop", 1.0f), 0.0f), "curve: empty keys evaluate to zero");
    }

    return test::finish("alpha curve tests");
}
