#ifndef WALLPAPER_ENGINE_DECODED_IMAGE_H
#define WALLPAPER_ENGINE_DECODED_IMAGE_H

#include <stdint.h>

#include <vector>

namespace wallpaper_engine {

enum class PixelFormat {
    Unknown,
    RGBA8,
    RG8,
    R8,
    BC1,
    BC2,
    BC3,
};

struct DecodedImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t data_size = 0;
    int channels = 0;
    PixelFormat format = PixelFormat::Unknown;

    bool valid() const {
        return !pixels.empty() && width > 0 && height > 0 && format != PixelFormat::Unknown;
    }
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_DECODED_IMAGE_H
