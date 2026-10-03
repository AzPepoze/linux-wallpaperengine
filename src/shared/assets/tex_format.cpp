#include "tex_decoder_internal.h"

namespace wallpaper_engine {
using namespace tex_internal;

namespace tex_internal {
FormatInfo getFormatInfo(uint32_t format_id) {
    switch (static_cast<TexFormat>(format_id)) {
        case TexFormat::RGBA8:
            return {"RGBA8", PixelFormat::RGBA8, 4, 4};
        case TexFormat::BC1_DXT1:
        case TexFormat::BC1_DXT1_ALT:
            return {"DXT1/BC1", PixelFormat::BC1, 4, 0};
        case TexFormat::BC2_DXT3:
        case TexFormat::BC2_DXT3_ALT:
            return {"DXT3/BC2", PixelFormat::BC2, 4, 0};
        case TexFormat::BC3_DXT5:
            return {"DXT5/BC3", PixelFormat::BC3, 4, 0};
        case TexFormat::RG8:
            return {"RG8", PixelFormat::RG8, 2, 2};
        case TexFormat::R8:
            return {"R8 (Grayscale)", PixelFormat::R8, 1, 1};
        default:
            return {"Unknown", PixelFormat::RGBA8, 4, 0};
    }
}

size_t getBlockCompressedBlockSize(PixelFormat format) {
    switch (format) {
        case PixelFormat::BC1:
            return 8;
        case PixelFormat::BC2:
        case PixelFormat::BC3:
            return 16;
        default:
            return 0;
    }
}

size_t expectedPixelDataSize(uint32_t width, uint32_t height, PixelFormat format) {
    const size_t pixel_count = static_cast<size_t>(width) * height;
    switch (format) {
        case PixelFormat::RGBA8:
            return pixel_count * 4;
        case PixelFormat::RG8:
            return pixel_count * 2;
        case PixelFormat::R8:
            return pixel_count;
        case PixelFormat::BC1:
            return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 8;
        case PixelFormat::BC2:
        case PixelFormat::BC3:
            return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 16;
        default:
            return 0;
    }
}
}  // namespace tex_internal

}  // namespace wallpaper_engine
