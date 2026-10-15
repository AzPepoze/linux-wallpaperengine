#pragma once

#include <cstdint>
#include <vector>

// Shrinks decoded RGBA8 pixels to a smaller size. Never enlarges: a target larger than the source is clamped to it.
namespace texture_resize {
enum class Filter { Box, Lanczos3 };

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, row-major, no padding
};

// Returns an empty image when the source is empty or the request is invalid.
Image resizeRgba8(const uint8_t* source, int source_width, int source_height, int target_width, int target_height,
                  Filter filter);

// Levels 1..n of a mip chain, each half the size of the one before (box filter), down to 1x1.
std::vector<Image> buildMipChain(const uint8_t* source, int width, int height);
}  // namespace texture_resize
