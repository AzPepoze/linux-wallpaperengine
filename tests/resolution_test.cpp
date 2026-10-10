#include "shared/core/resolution.h"

#include <limits>

#include "test_util.h"

int main() {
    using resolution::targetSize;
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

    resolution::Setting setting;
    CHECK(resolution::parse("auto", setting) && setting.mode == resolution::Setting::Mode::Auto);
    CHECK(resolution::parse("native", setting) && setting.mode == resolution::Setting::Mode::Native);
    CHECK(resolution::parse("3840x2160", setting));
    CHECK(setting.mode == resolution::Setting::Mode::Fixed && setting.width == 3840 && setting.height == 2160);
    CHECK(!resolution::parse("", setting));
    CHECK(!resolution::parse("1920", setting));
    CHECK(!resolution::parse("1920x", setting));
    CHECK(!resolution::parse("x1080", setting));
    CHECK(!resolution::parse("0x1080", setting));
    CHECK(!resolution::parse("-1920x1080", setting));
    CHECK(!resolution::parse("1920X1080", setting));
    CHECK(!resolution::parse("1920x1080x2", setting));
    CHECK(!resolution::parse("huge", setting));
    CHECK(setting.mode == resolution::Setting::Mode::Fixed && setting.width == 3840);

    CHECK(resolution::referenceRatio(resolution::Setting{}, 1920, 1080) == 1.0);
    CHECK(resolution::referenceRatio(resolution::Setting{resolution::Setting::Mode::Native, 0, 0}, 1920, 1080) == 1.0);
    const resolution::Setting uhd{resolution::Setting::Mode::Fixed, 3840, 2160};
    CHECK(resolution::referenceRatio(uhd, 1920, 1080) == 2.0);
    CHECK(resolution::referenceRatio(uhd, 3840, 2160) == 1.0);
    CHECK(resolution::referenceRatio(uhd, 0, 1080) == 1.0);
    return test::finish("resolution checks");
}
