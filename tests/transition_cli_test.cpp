#include <string>

#include "test_util.h"
#include "wallpaper/transition/transition_catalog.h"

using namespace lwe::transition;

int main() {
    int value = 0;
    int duration = 0;
    std::string error;
    CHECK(resolveTransitionSetting("crt", value, duration, error));
    CHECK(value == (int)Effect::Crt && duration == 1000);
    CHECK(resolveTransitionSetting("random", value, duration, error));
    CHECK(value == kSelectionRandom);
    CHECK(resolveTransitionSetting("none", value, duration, error));
    CHECK(value == kSelectionNone);
    CHECK(!resolveTransitionSetting("bogus", value, duration, error));
    return test::finish("transition cli checks");
}
