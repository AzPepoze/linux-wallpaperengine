#include "wallpaper/transition/transition_catalog.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "test_util.h"

using namespace lwe::transition;

int main() {
    CHECK(effectCount() == 27);
    CHECK(effectByIndex(0) && std::strcmp(effectByIndex(0)->name, "fade") == 0);
    CHECK(effectByIndex(21) && std::strcmp(effectByIndex(21)->name, "crt") == 0);
    CHECK(effectByIndex(26) && std::strcmp(effectByIndex(26)->name, "boilover") == 0);
    CHECK(effectByIndex(27) == nullptr);
    CHECK(effectByIndex(-1) == nullptr);
    CHECK(effectByIndex(16)->needs_geometry);
    CHECK(effectByIndex(23)->needs_geometry);
    CHECK(!effectByIndex(0)->needs_geometry);
    CHECK(effectByName("fade") == effectByIndex(0));
    CHECK(effectByName("glass_shatter") == effectByIndex(23));
    CHECK(effectByName("21") == effectByIndex(21));
    CHECK(effectByName("nope") == nullptr);

    Selection selection{};
    CHECK(parseSelection("fade", selection) && selection.value == (int)Effect::Fade);
    CHECK(parseSelection("26", selection) && selection.value == (int)Effect::Boilover);
    CHECK(parseSelection("none", selection) && selection.value == kSelectionNone);
    CHECK(parseSelection("random", selection) && selection.value == kSelectionRandom);
    CHECK(!parseSelection("", selection));
    CHECK(!parseSelection("99", selection));

    CHECK(pickRandomEffect(1234) >= 0 && pickRandomEffect(1234) <= 26);
    CHECK(pickRandomEffect(1234) == pickRandomEffect(1234));

    CHECK(transitionProgress(0.0f, 1000) == 0.0f);
    CHECK(transitionProgress(0.5f, 1000) == 0.5f);
    CHECK(transitionProgress(2.0f, 1000) == 1.0f);
    CHECK(transitionProgress(0.5f, 0) == 1.0f);

    Mode mode = Mode::Continue;
    CHECK(parseMode("freeze", mode) && mode == Mode::Freeze);
    CHECK(parseMode("continue", mode) && mode == Mode::Continue);
    CHECK(parseMode("FREEZE", mode) && mode == Mode::Freeze);
    CHECK(!parseMode("bogus", mode));

    std::string error;
    bool continue_previous = true;
    CHECK(resolveTransitionModeSetting("", continue_previous, error) && !continue_previous);
    CHECK(resolveTransitionModeSetting("freeze", continue_previous, error) && !continue_previous);
    CHECK(resolveTransitionModeSetting("continue", continue_previous, error) && continue_previous);
    CHECK(!resolveTransitionModeSetting("bogus", continue_previous, error) && !error.empty());
    return test::finish("transition catalog checks");
}
