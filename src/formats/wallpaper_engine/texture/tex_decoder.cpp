#include "tex_decoder.h"

#include <stdio.h>
#include <string.h>

#include <vector>

#include "core/logger.h"
#include "lz4.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TAG "TEXTURE"

namespace wallpaper_engine {
namespace {

uint32_t readU32(FILE* file) {
    uint32_t value = 0;
    if (fread(&value, sizeof(value), 1, file) != 1) return 0;
    return value;
}

void readString(FILE* file, char* buffer, int size) {
    if (!buffer || size <= 0) return;
    fread(buffer, 1, size, file);
    buffer[size] = '\0';
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

    image.width = (uint32_t)width;
    image.height = (uint32_t)height;
    image.channels = 4;
    image.format = PixelFormat::RGBA8;
    image.data_size = image.width * image.height * 4;
    image.pixels.assign(pixels, pixels + image.data_size);
    stbi_image_free(pixels);

    LOG_TAG_I(TAG, "Loaded image: %s (%dx%d)", path, width, height);
    return image;
}

}  // namespace

DecodedImage decodeTexture(const char* path, int image_index) {
    DecodedImage image;
    if (!path) return image;

    const char* extension = strrchr(path, '.');
    if (!extension || strcmp(extension, ".tex") != 0) return decodeStandardImage(path, image_index);

    FILE* file = fopen(path, "rb");
    if (!file) {
        LOG_TAG_E(TAG, "Failed to open texture: %s", path);
        return image;
    }

    char magic[9] = {};
    readString(file, magic, 8);
    fseek(file, 1, SEEK_CUR);
    fseek(file, 8, SEEK_CUR);
    fseek(file, 1, SEEK_CUR);

    if (strncmp(magic, "TEXV", 4) != 0) {
        LOG_TAG_E(TAG, "Invalid .tex magic: %s in %s", magic, path);
        fclose(file);
        return image;
    }

    const uint32_t wallpaper_format = readU32(file);
    fseek(file, 4, SEEK_CUR);
    readU32(file);  // allocated texture width; retained for future allocated/content-size support
    fseek(file, 4, SEEK_CUR);
    const uint32_t image_width = readU32(file);
    const uint32_t image_height = readU32(file);

    fseek(file, 4, SEEK_CUR);
    char container_magic[9] = {};
    readString(file, container_magic, 8);
    fseek(file, 1, SEEK_CUR);
    const uint32_t image_count = readU32(file);

    LOG_TAG_I(TAG, "Loading texture: %s (images: %u, requested: %d)", path, image_count, image_index);
    if (image_index < 0 || image_index >= (int)image_count) {
        LOG_TAG_W(TAG, "Requested image index %d out of bounds (count: %u)", image_index, image_count);
        fclose(file);
        return image;
    }

    const char* format_name = "Unknown";
    switch (wallpaper_format) {
        case 0:
            format_name = "RGBA8";
            break;
        case 4:
            format_name = "DXT1/BC1";
            break;
        case 5:
            format_name = "DXT3/BC2";
            break;
        case 6:
            format_name = "DXT5/BC3";
            break;
        case 8:
            format_name = "RGBA8";
            break;
        case 9:
            format_name = "R8 (Grayscale)";
            break;
        default:
            break;
    }

    LOG_TAG_D(TAG, "  .tex version: %s", magic);
    LOG_TAG_D(TAG, "  Format: %s (wp:%u), Size: %ux%u, Container: %s", format_name, wallpaper_format,
              image_width, image_height, container_magic);

    if (strcmp(container_magic, "TEXB0003") == 0) readU32(file);

    for (uint32_t image_number = 0; image_number < image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            readU32(file);  // mip width
            readU32(file);  // mip height

            bool compressed_lz4 = false;
            uint32_t decompressed_size = 0;
            if (strcmp(container_magic, "TEXB0001") != 0) {
                compressed_lz4 = readU32(file) == 1;
                decompressed_size = readU32(file);
            }

            const uint32_t data_size = readU32(file);
            const bool requested = image_number == (uint32_t)image_index && mip == 0;
            if (!requested) {
                fseek(file, data_size, SEEK_CUR);
                continue;
            }

            std::vector<uint8_t> data_block(data_size);
            if (data_size > 0 && fread(data_block.data(), 1, data_size, file) != data_size) {
                LOG_TAG_E(TAG, "Failed to read texture data: %s", path);
                fclose(file);
                return {};
            }

            if (data_size > 8 && data_block[0] == 0x89 && data_block[1] == 'P' && data_block[2] == 'N' &&
                data_block[3] == 'G') {
                int width = 0;
                int height = 0;
                int channels = 0;
                stbi_uc* pixels = stbi_load_from_memory(data_block.data(), (int)data_block.size(), &width, &height,
                                                        &channels, 4);
                if (pixels) {
                    image.width = (uint32_t)width;
                    image.height = (uint32_t)height;
                    image.channels = 4;
                    image.format = PixelFormat::RGBA8;
                    image.data_size = image.width * image.height * 4;
                    image.pixels.assign(pixels, pixels + image.data_size);
                    stbi_image_free(pixels);
                }
                fclose(file);
                return image;
            }

            std::vector<uint8_t> raw_data;
            if (compressed_lz4) {
                raw_data.resize(decompressed_size);
                const int decoded = LZ4_decompress_safe((const char*)data_block.data(), (char*)raw_data.data(),
                                                        (int)data_block.size(), (int)raw_data.size());
                if (decoded < 0) {
                    LOG_TAG_E(TAG, "LZ4 decompression failed: %s", path);
                    fclose(file);
                    return {};
                }
                raw_data.resize((size_t)decoded);
            } else {
                raw_data = std::move(data_block);
            }

            image.width = image_width;
            image.height = image_height;
            image.channels = 4;

            switch (wallpaper_format) {
                case 0:
                case 8:
                    image.format = PixelFormat::RGBA8;
                    image.pixels = std::move(raw_data);
                    break;
                case 4:
                    image.format = PixelFormat::BC1;
                    image.pixels = std::move(raw_data);
                    break;
                case 5:
                    image.format = PixelFormat::BC2;
                    image.pixels = std::move(raw_data);
                    break;
                case 6:
                    image.format = PixelFormat::BC3;
                    image.pixels = std::move(raw_data);
                    break;
                case 9:
                    image.format = PixelFormat::R8;
                    image.channels = 1;
                    image.pixels = std::move(raw_data);
                    break;
                default:
                    LOG_TAG_E(TAG, "Unsupported Wallpaper Engine texture format: %u", wallpaper_format);
                    fclose(file);
                    return {};
            }

            image.data_size = (uint32_t)image.pixels.size();
            fclose(file);
            return image;
        }
    }

    fclose(file);
    return image;
}

}  // namespace wallpaper_engine
