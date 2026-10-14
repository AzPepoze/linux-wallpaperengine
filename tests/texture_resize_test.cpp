#include "shared/assets/texture_resize.h"

#include <cstdlib>
#include <vector>

#include "test_util.h"

namespace {
using texture_resize::Filter;

std::vector<uint8_t> solid(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
    return pixels;
}

// Vertical stripes: even columns black, odd columns white, alpha 255.
std::vector<uint8_t> stripes(int width, int height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint8_t v = (x % 2) ? 255 : 0;
            uint8_t* px = &pixels[(static_cast<size_t>(y) * width + x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    return pixels;
}

bool allPixelsEqual(const texture_resize::Image& image, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (size_t i = 0; i < image.pixels.size(); i += 4)
        if (image.pixels[i] != r || image.pixels[i + 1] != g || image.pixels[i + 2] != b || image.pixels[i + 3] != a)
            return false;
    return true;
}
}  // namespace

int main() {
    // Size: target is used as given when it is smaller, odd ratios included.
    const auto src = solid(9, 6, 10, 20, 30, 255);
    const auto five_by_two = texture_resize::resizeRgba8(src.data(), 9, 6, 5, 2, Filter::Box);
    CHECK(five_by_two.width == 5 && five_by_two.height == 2);
    CHECK(five_by_two.pixels.size() == 5u * 2u * 4u);

    // Never upscale: a larger target is clamped to the source size.
    const auto clamped = texture_resize::resizeRgba8(src.data(), 9, 6, 90, 60, Filter::Lanczos3);
    CHECK(clamped.width == 9 && clamped.height == 6);

    // Solid colour stays exact under both filters, including the alpha channel.
    CHECK(allPixelsEqual(texture_resize::resizeRgba8(src.data(), 9, 6, 4, 3, Filter::Box), 10, 20, 30, 255));
    CHECK(allPixelsEqual(texture_resize::resizeRgba8(src.data(), 9, 6, 4, 3, Filter::Lanczos3), 10, 20, 30, 255));

    // 2:1 box over black/white stripes averages each pair to mid grey.
    const auto striped = stripes(8, 1);
    const auto half = texture_resize::resizeRgba8(striped.data(), 8, 1, 4, 1, Filter::Box);
    CHECK(half.width == 4 && half.height == 1);
    for (int x = 0; x < 4; ++x) CHECK(std::abs(half.pixels[x * 4] - 128) <= 1);

    // Alpha is resized like colour: half transparent stays half transparent.
    const auto translucent = solid(4, 4, 0, 0, 0, 128);
    CHECK(allPixelsEqual(texture_resize::resizeRgba8(translucent.data(), 4, 4, 2, 2, Filter::Box), 0, 0, 0, 128));

    // Lanczos keeps the same average brightness as box for a smooth input.
    const auto lanczos_half = texture_resize::resizeRgba8(striped.data(), 8, 1, 4, 1, Filter::Lanczos3);
    CHECK(lanczos_half.width == 4);
    CHECK(std::abs(lanczos_half.pixels[0] - 128) <= 40);

    // Invalid input returns an empty image instead of reading out of range.
    CHECK(texture_resize::resizeRgba8(nullptr, 9, 6, 4, 3, Filter::Box).pixels.empty());
    CHECK(texture_resize::resizeRgba8(src.data(), 0, 6, 4, 3, Filter::Box).pixels.empty());
    CHECK(texture_resize::resizeRgba8(src.data(), 9, 6, 0, 3, Filter::Box).pixels.empty());
    CHECK(texture_resize::resizeRgba8(src.data(), 9, 6, 4, -1, Filter::Lanczos3).pixels.empty());

    // Mip chain: halves each level down to 1x1; a 1x1 source has no levels below it.
    const auto chain = texture_resize::buildMipChain(src.data(), 9, 6);
    CHECK(chain.size() == 3);
    CHECK(chain.size() == 3 && chain[0].width == 4 && chain[0].height == 3);
    CHECK(chain.size() == 3 && chain[1].width == 2 && chain[1].height == 1);
    CHECK(chain.size() == 3 && chain[2].width == 1 && chain[2].height == 1);
    for (const auto& level : chain) CHECK(allPixelsEqual(level, 10, 20, 30, 255));
    CHECK(texture_resize::buildMipChain(src.data(), 1, 1).empty());
    CHECK(texture_resize::buildMipChain(nullptr, 9, 6).empty());

    return test::finish("texture resize checks");
}
