#include "shared/graphics/backend/readback_pixels.h"

#include <array>
#include <limits>

#include "test_util.h"

int main() {
    CHECK(readbackPixelSize(SG_PIXELFORMAT_RGBA32F) == 16);
    CHECK(readbackPixelSize(SG_PIXELFORMAT_RGBA16F) == 8);
    CHECK(readbackPixelSize(SG_PIXELFORMAT_BC1_RGBA) == 0);
    const std::array<float, 8> floats = {2, 0.5f, -1, 0.25f, 0, 1, std::numeric_limits<float>::quiet_NaN(), 1};
    std::array<uint8_t, 8> output = {};
    convertReadbackPixels((const uint8_t*)floats.data(), output.data(), 2, SG_PIXELFORMAT_RGBA32F);
    CHECK(output == (std::array<uint8_t, 8>{255, 128, 0, 64, 0, 255, 0, 255}));
    const uint16_t half[] = {0x3C00, 0x3800, 0, 0x3C00};
    convertReadbackPixels((const uint8_t*)half, output.data(), 1, SG_PIXELFORMAT_RGBA16F);
    CHECK(output[0] == 255 && output[1] == 128 && output[2] == 0 && output[3] == 255);
    const uint8_t bgra[] = {10, 20, 30, 40};
    convertReadbackPixels(bgra, output.data(), 1, SG_PIXELFORMAT_BGRA8);
    CHECK(output[0] == 30 && output[1] == 20 && output[2] == 10 && output[3] == 40);
    convertReadbackPixels(bgra, output.data(), 1, SG_PIXELFORMAT_R8);
    CHECK(output[0] == 10 && output[1] == 0 && output[2] == 0 && output[3] == 255);
    CHECK(readbackHalf(1) > 0 && readbackHalf(0x8000) == 0);
    return test::finish("readback pixel checks");
}
