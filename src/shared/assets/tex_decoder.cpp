#include "tex_decoder.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "lz4.h"
#include "shared/core/logger.h"
#include "tex_decoder_internal.h"

#define TAG "TEXTURE"

namespace wallpaper_engine {
using namespace tex_internal;

DecodedImage decodeTexture(const char* path, int image_index) {
    if (!path) return {};

    const char* extension = std::strrchr(path, '.');
    if (!extension || std::strcmp(extension, ".tex") != 0) {
        return decodeStandardImage(path, image_index);
    }

    ScopedFile file(path);
    if (!file.isOpen()) {
        LOG_TAG_E(TAG, "Failed to open texture: %s", path);
        return {};
    }

    TexHeader header;
    if (!readTextureHeader(file, header)) {
        LOG_TAG_E(TAG, "Invalid .tex magic in %s", path);
        return {};
    }

    LOG_TAG_I(TAG, "Loading texture: %s (images: %u, requested: %d)", path, header.image_count, image_index);
    if (image_index < 0 || image_index >= static_cast<int>(header.image_count)) {
        LOG_TAG_W(TAG, "Requested image index %d out of bounds (count: %u)", image_index, header.image_count);
        return {};
    }

    const FormatInfo format = getFormatInfo(header.format_id);
    LOG_TAG_D(TAG, "  .tex version: %s", header.version_magic);
    LOG_TAG_D(TAG, "  Format: %s (wp:%u), Size: %ux%u, Container: %s", format.name, header.format_id,
              header.image_width, header.image_height, header.container_magic);

    for (uint32_t image_number = 0; image_number < header.image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            const uint32_t mip_width = readU32(file);
            const uint32_t mip_height = readU32(file);
            if ((header.flags & 0x40) != 0) {
                readU32(file);  // mip depth
            }

            bool compressed_lz4 = false;
            uint32_t decompressed_size = 0;
            if (std::strcmp(header.container_magic, "TEXB0001") != 0) {
                compressed_lz4 = readU32(file) == 1;
                decompressed_size = readU32(file);
            }

            const uint32_t data_size = readU32(file);
            const bool requested = (image_number == static_cast<uint32_t>(image_index)) && (mip == 0);
            if (!requested) {
                std::fseek(file, static_cast<long>(data_size), SEEK_CUR);
                continue;
            }

            std::vector<uint8_t> data_block(data_size);
            if (data_size > 0 && std::fread(data_block.data(), 1, data_size, file) != data_size) {
                LOG_TAG_E(TAG, "Failed to read texture data: %s", path);
                return {};
            }

            if (isVideoContainer(data_block.data(), data_block.size())) {
                LOG_TAG_D(TAG, "Detected embedded MP4 payload in %s", path);
                DecodedImage video_image;
                video_image.is_video = true;
                return video_image;
            }

            DecodedImage image;
            if (tryDecodeEmbeddedImage(data_block.data(), data_block.size(), image)) {
                return image;
            }

            std::vector<uint8_t> raw_data;
            if (compressed_lz4) {
                if (!decompressLz4Payload(data_block.data(), data_block.size(), decompressed_size, raw_data)) {
                    LOG_TAG_E(TAG, "LZ4 decompression failed: %s", path);
                    return {};
                }
            } else {
                raw_data = std::move(data_block);
            }

            image.width = header.image_width;
            image.height = header.image_height;
            image.channels = format.channels;
            image.format = format.pixel_format;

            // Mip dimensions are GPU-aligned, so a mip can be larger than the image.
            const size_t bpp = format.bytes_per_pixel;
            const size_t block_bytes = getBlockCompressedBlockSize(image.format);
            const size_t mip_expected = expectedPixelDataSize(mip_width, mip_height, image.format);
            const size_t img_expected = expectedPixelDataSize(header.image_width, header.image_height, image.format);

            const bool is_uncompressed_padded =
                (bpp > 0) && (mip_width != header.image_width || mip_height != header.image_height) &&
                (raw_data.size() == static_cast<size_t>(mip_width) * mip_height * bpp) &&
                (header.image_width <= mip_width) && (header.image_height <= mip_height);

            // Block-compressed mip grid is larger than the image grid; crop to the image grid.
            const bool is_bc_padded = (block_bytes > 0) && (img_expected > 0) && (mip_expected > 0) &&
                                      (raw_data.size() == mip_expected) && (mip_expected != img_expected) &&
                                      (mip_width >= header.image_width) && (mip_height >= header.image_height);

            if (is_uncompressed_padded) {
                image.pixels =
                    unpadPaddedRows(raw_data.data(), header.image_width, header.image_height, mip_width, bpp);
            } else if (is_bc_padded) {
                image.pixels = unpadBlockCompressedRows(raw_data.data(), header.image_width, header.image_height,
                                                        mip_width, block_bytes);
            } else if (raw_data.size() == mip_expected && mip_expected == img_expected) {
                image.pixels = std::move(raw_data);
            } else if (raw_data.size() == mip_expected) {
                image.width = mip_width;
                image.height = mip_height;
                image.pixels = std::move(raw_data);
            } else {
                image.pixels = std::move(raw_data);
            }

            image.data_size = static_cast<uint32_t>(image.pixels.size());
            const size_t expected_size = expectedPixelDataSize(image.width, image.height, image.format);
            if (expected_size == 0 || image.pixels.size() != expected_size) {
                LOG_TAG_E(TAG,
                          "Texture data size mismatch for %s: format %s at %ux%u requires %zu bytes, got %zu; "
                          "skipping unsupported payload",
                          path, format.name, image.width, image.height, expected_size, image.pixels.size());
                return {};
            }

            return image;
        }
    }

    return {};
}

}  // namespace wallpaper_engine
