#include "app/platform/layer_options.h"

#include <cstdio>
#include <string>
#include <vector>

#include "test_util.h"

using namespace layer_options;

static void checkLayers() {
    Layer layer = Layer::Top;
    CHECK(parseLayer("", layer) && layer == Layer::Background);
    CHECK(parseLayer("background", layer) && layer == Layer::Background);
    CHECK(parseLayer("Bottom", layer) && layer == Layer::Bottom);
    CHECK(parseLayer("top", layer) && layer == Layer::Top);
    CHECK(parseLayer("OVERLAY", layer) && layer == Layer::Overlay);
    layer = Layer::Top;
    CHECK(!parseLayer("middle", layer) && layer == Layer::Top);
}

static void checkSizes() {
    int w = 0;
    int h = 0;
    CHECK(parseSize("320x180", w, h) && w == 320 && h == 180);
    CHECK(parseSize("64X48", w, h) && w == 64 && h == 48);
    CHECK(!parseSize("320", w, h));
    CHECK(!parseSize("x180", w, h));
    CHECK(!parseSize("0x180", w, h));
    CHECK(!parseSize("-3x4", w, h));
    CHECK(!parseSize("3x4x5", w, h));
    CHECK(w == 64 && h == 48);
}

static void checkAnchors() {
    uint32_t mask = 0;
    CHECK(parseAnchor("all", mask) && mask == kAnchorAll);
    CHECK(parseAnchor("top-left", mask) && mask == (kAnchorTop | kAnchorLeft));
    CHECK(parseAnchor("bottom,right", mask) && mask == (kAnchorBottom | kAnchorRight));
    CHECK(parseAnchor("Left", mask) && mask == kAnchorLeft);
    mask = 7;
    CHECK(!parseAnchor("top-middle", mask) && mask == 7);
    CHECK(!parseAnchor("", mask));
    CHECK(!parseAnchor("top-", mask));
}

static void checkOutputs() {
    const std::vector<std::string> names = {"HDMI-A-1", "DP-4", "eDP-1"};
    CHECK(findOutput(names, "DP-4") == 1);
    CHECK(findOutput(names, "dp-4") == 1);
    CHECK(findOutput(names, "DP-5") == -1);
    CHECK(findOutput(names, "") == -1);
    CHECK(findOutput({}, "DP-4") == -1);
}

int main() {
    checkLayers();
    checkSizes();
    checkAnchors();
    checkOutputs();
    return test::finish("layer option checks");
}
