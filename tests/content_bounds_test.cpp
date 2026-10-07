#include "shared/assets/content_bounds.h"

#include "test_util.h"

namespace {
using wallpaper_engine::DecodedImage;
using wallpaper_engine::PixelFormat;

DecodedImage makeImage(PixelFormat format, uint32_t width, uint32_t height, size_t bytes) {
    DecodedImage image;
    image.format = format;
    image.width = width;
    image.height = height;
    image.pixels.assign(bytes, 0);
    return image;
}

// Writes a BC3 block whose alpha indices all select `index` (endpoints a0, a1).
void setBc3Block(DecodedImage& image, uint32_t cols, uint32_t bx, uint32_t by, uint8_t a0, uint8_t a1, uint8_t index) {
    uint8_t* block = &image.pixels[((size_t)by * cols + bx) * 16];
    block[0] = a0;
    block[1] = a1;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= (uint64_t)index << (3 * i);
    for (int i = 0; i < 6; ++i) block[2 + i] = (uint8_t)(bits >> (8 * i));
}
}  // namespace

int main() {
    using content_bounds::Rect;

    // RGBA8: a single visible texel at (1,2) of a 4x4 image.
    DecodedImage rgba = makeImage(PixelFormat::RGBA8, 4, 4, 4 * 4 * 4);
    rgba.pixels[(2 * 4 + 1) * 4 + 3] = 200;
    Rect rect = content_bounds::fromImage(rgba);
    CHECK(rect.valid);
    CHECK(rect.u0 == 0.25f && rect.v0 == 0.5f && rect.u1 == 0.5f && rect.v1 == 0.75f);

    // Fully transparent and unsupported images give no bounds.
    CHECK(!content_bounds::fromImage(makeImage(PixelFormat::RGBA8, 4, 4, 64)).valid);
    CHECK(!content_bounds::fromImage(makeImage(PixelFormat::BC1, 8, 8, 64)).valid);
    CHECK(!content_bounds::fromImage(DecodedImage{}).valid);

    // BC3, 8x8 = 2x2 blocks: only the top-right block is visible (endpoint-driven alpha).
    DecodedImage bc3 = makeImage(PixelFormat::BC3, 8, 8, 4 * 16);
    setBc3Block(bc3, 2, 1, 0, 255, 0, 0);
    rect = content_bounds::fromImage(bc3);
    CHECK(rect.valid);
    CHECK(rect.u0 == 0.5f && rect.v0 == 0.0f && rect.u1 == 1.0f && rect.v1 == 0.5f);

    // Six-value alpha mode with every index at 6 is fully transparent, with index 7 fully opaque.
    DecodedImage bc3_six = makeImage(PixelFormat::BC3, 8, 8, 4 * 16);
    for (uint32_t i = 0; i < 4; ++i) setBc3Block(bc3_six, 2, i % 2, i / 2, 0, 255, 6);
    CHECK(!content_bounds::fromImage(bc3_six).valid);
    setBc3Block(bc3_six, 2, 0, 1, 0, 255, 7);
    rect = content_bounds::fromImage(bc3_six);
    CHECK(rect.valid);
    CHECK(rect.u0 == 0.0f && rect.v0 == 0.5f && rect.u1 == 0.5f && rect.v1 == 1.0f);

    // BC2: explicit 4-bit alpha, bottom-right block visible.
    DecodedImage bc2 = makeImage(PixelFormat::BC2, 8, 8, 4 * 16);
    bc2.pixels[3 * 16 + 5] = 0x10;
    rect = content_bounds::fromImage(bc2);
    CHECK(rect.valid);
    CHECK(rect.u0 == 0.5f && rect.v0 == 0.5f && rect.u1 == 1.0f && rect.v1 == 1.0f);

    // Truncated block data is rejected instead of read past the end.
    CHECK(!content_bounds::fromImage(makeImage(PixelFormat::BC3, 8, 8, 16)).valid);

    // Expansion clamps to the unit square; pixel conversion pads and clamps to the target.
    const Rect grown = content_bounds::expand({0.1f, 0.1f, 0.9f, 0.9f, true}, 0.2f);
    CHECK(grown.u0 == 0.0f && grown.v0 == 0.0f && grown.u1 == 1.0f && grown.v1 == 1.0f);
    const content_bounds::PixelRect pixels = content_bounds::toPixels({0.25f, 0.5f, 0.5f, 0.75f, true}, 100, 200);
    CHECK(pixels.x == 23 && pixels.y == 98 && pixels.width == 29 && pixels.height == 54);
    const content_bounds::PixelRect all = content_bounds::toPixels({0.0f, 0.0f, 1.0f, 1.0f, true}, 100, 200);
    CHECK(all.x == 0 && all.y == 0 && all.width == 100 && all.height == 200);

    // Only resampling passes are crop-safe, and their reach comes from the live uniform values.
    const std::map<std::string, std::vector<float>> shake = {{"g_Amp", {0.1f}}};
    const std::map<std::string, std::vector<float>> waves = {{"g_Strength", {0.2f}}};
    CHECK(content_bounds::passDisplacement("effects/shake", shake).value() > 0.0099f);
    CHECK(content_bounds::passDisplacement("effects/shake", shake).value() < 0.0101f);
    CHECK(content_bounds::passDisplacement("effects/waterwaves", waves).value() > 0.0399f);
    CHECK(!content_bounds::passDisplacement("effects/shake", waves));
    CHECK(!content_bounds::passDisplacement("effects/tint", shake));
    return test::finish("content bounds checks");
}
