#include "shared/core/antialias.h"

#include "test_util.h"

int main() {
    using antialias::Mode;

    Mode mode = Mode::Fxaa;
    CHECK(antialias::parse("off", mode) && mode == Mode::Off);
    CHECK(antialias::parse("fxaa", mode) && mode == Mode::Fxaa);
    CHECK(antialias::parse("smaa", mode) && mode == Mode::Smaa);
    CHECK(antialias::parse("msaa2", mode) && mode == Mode::Msaa2);
    CHECK(antialias::parse("msaa4", mode) && mode == Mode::Msaa4);
    CHECK(antialias::parse("msaa8", mode) && mode == Mode::Msaa8);
    CHECK(!antialias::parse("", mode));
    CHECK(!antialias::parse("MSAA4", mode));
    CHECK(!antialias::parse("msaa16", mode));
    CHECK(!antialias::parse("taa", mode));
    CHECK(mode == Mode::Msaa8);

    CHECK(antialias::sampleCount(Mode::Off) == 1);
    CHECK(antialias::sampleCount(Mode::Fxaa) == 1);
    CHECK(antialias::sampleCount(Mode::Smaa) == 1);
    CHECK(antialias::sampleCount(Mode::Msaa2) == 2);
    CHECK(antialias::sampleCount(Mode::Msaa4) == 4);
    CHECK(antialias::sampleCount(Mode::Msaa8) == 8);

    CHECK(!antialias::isMsaa(Mode::Fxaa));
    CHECK(antialias::isMsaa(Mode::Msaa2));
    CHECK(antialias::isPostPass(Mode::Fxaa));
    CHECK(antialias::isPostPass(Mode::Smaa));
    CHECK(!antialias::isPostPass(Mode::Msaa4));
    CHECK(!antialias::isPostPass(Mode::Off));

    CHECK(antialias::describe(Mode::Msaa4) == "MSAA 4x");
    CHECK(antialias::describe(Mode::Off) == "off");
    return test::finish("antialias checks");
}
