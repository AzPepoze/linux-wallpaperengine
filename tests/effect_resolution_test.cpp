#include "wallpaper/2d/layers/image/effect_resolution.h"

#include <limits>

#include "test_util.h"

int main() {
    using effect_resolution::targetSize;
    const std::pair<int, int> hd = {1920, 1080}, native = {3840, 2160};
    CHECK(targetSize(3840, 2160, 1920, 1080) == hd);
    CHECK(targetSize(3840, 2160, -1920, -1080) == hd);
    CHECK(targetSize(3840, 2160, 3840, 2160) == native);
    CHECK(targetSize(3840, 2160, 7680, 4320) == native);
    CHECK(targetSize(3840, 2160, 1280, 720) == std::make_pair(1280, 720));
    // Stretch uses the larger sampling density, preserving the source aspect.
    CHECK(targetSize(3840, 2160, 1920, 1440) == std::make_pair(2560, 1440));
    CHECK(targetSize(3840, 2160, 0, 0) == std::make_pair(1, 1));
    CHECK(targetSize(3840, 2160, 1920.1, 1080) == std::make_pair(1921, 1081));
    CHECK(targetSize(3840, 2160, std::numeric_limits<double>::infinity(), 1080) == native);
    CHECK(effect_resolution::fingerprint("a\r\nb") == effect_resolution::fingerprint("a\nb"));
    CHECK(effect_resolution::fingerprint("shader") != effect_resolution::fingerprint("modified shader"));
    CHECK(!effect_resolution::verifiedShake("", ""));
    CHECK(!effect_resolution::verifiedShake("custom vertex", "custom fragment"));
    return test::finish("effect resolution checks");
}
