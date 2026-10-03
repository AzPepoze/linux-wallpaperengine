#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "lz4.h"
#include "shared/core/logger.h"
#include "tex_decoder.h"
#include "tex_decoder_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TAG "TEXTURE"

namespace wallpaper_engine {
using namespace tex_internal;

namespace tex_internal {
bool tryDecodeEmbeddedImage(const uint8_t* data, size_t size, DecodedImage& out_image) {
    if (!data || size < 4) return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &channels)) {
        return false;
    }

    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) return false;

    out_image.width = static_cast<uint32_t>(width);
    out_image.height = static_cast<uint32_t>(height);
    out_image.channels = 4;
    out_image.format = PixelFormat::RGBA8;
    out_image.data_size = out_image.width * out_image.height * 4;
    out_image.pixels.assign(pixels, pixels + out_image.data_size);
    stbi_image_free(pixels);
    return true;
}

bool decompressLz4Payload(const uint8_t* compressed_data, size_t compressed_size, uint32_t decompressed_size,
                          std::vector<uint8_t>& out_decompressed) {
    out_decompressed.resize(decompressed_size);
    const int decoded = LZ4_decompress_safe(reinterpret_cast<const char*>(compressed_data),
                                            reinterpret_cast<char*>(out_decompressed.data()),
                                            static_cast<int>(compressed_size), static_cast<int>(decompressed_size));
    if (decoded < 0) return false;
    out_decompressed.resize(static_cast<size_t>(decoded));
    return true;
}

std::vector<uint8_t> unpadPaddedRows(const uint8_t* src, uint32_t img_w, uint32_t img_h, uint32_t mip_w, size_t bpp) {
    std::vector<uint8_t> unpadded(static_cast<size_t>(img_w) * img_h * bpp);
    for (uint32_t y = 0; y < img_h; ++y) {
        std::memcpy(unpadded.data() + static_cast<size_t>(y) * img_w * bpp, src + static_cast<size_t>(y) * mip_w * bpp,
                    static_cast<size_t>(img_w) * bpp);
    }
    return unpadded;
}

std::vector<uint8_t> unpadBlockCompressedRows(const uint8_t* src, uint32_t img_w, uint32_t img_h, uint32_t mip_w,
                                              size_t block_bytes) {
    const uint32_t img_blocks_x = (img_w + 3) / 4;
    const uint32_t img_blocks_y = (img_h + 3) / 4;
    const uint32_t mip_blocks_x = (mip_w + 3) / 4;
    const size_t row_bytes_to_copy = static_cast<size_t>(img_blocks_x) * block_bytes;
    const size_t mip_row_stride = static_cast<size_t>(mip_blocks_x) * block_bytes;

    std::vector<uint8_t> unpadded(static_cast<size_t>(img_blocks_x) * img_blocks_y * block_bytes);
    for (uint32_t by = 0; by < img_blocks_y; ++by) {
        std::memcpy(unpadded.data() + static_cast<size_t>(by) * row_bytes_to_copy,
                    src + static_cast<size_t>(by) * mip_row_stride, row_bytes_to_copy);
    }
    return unpadded;
}

DecodedImage decodeStandardImage(const char* path, int image_index) {
    DecodedImage image;
    if (image_index > 0) return image;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        LOG_TAG_E(TAG, "Failed to load image: %s", path);
        return image;
    }

    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.channels = 4;
    image.format = PixelFormat::RGBA8;
    image.data_size = image.width * image.height * 4;
    image.pixels.assign(pixels, pixels + image.data_size);
    stbi_image_free(pixels);

    LOG_TAG_I(TAG, "Loaded image: %s (%dx%d)", path, width, height);
    return image;
}
}  // namespace tex_internal

bool isVideoContainer(const uint8_t* data, size_t size) {
    if (!data || size < 8) return false;
    if (std::memcmp(data + 4, "ftyp", 4) != 0) return false;
    const uint32_t box_size = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                              (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    return box_size >= 8 && box_size <= size;
}

}  // namespace wallpaper_engine
