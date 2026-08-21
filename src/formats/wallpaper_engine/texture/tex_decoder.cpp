#include "tex_decoder.h"

#include <math.h>
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

constexpr uint32_t kTextureFlagAnimated = 4;

uint32_t readU32(FILE* file) {
    uint32_t value = 0;
    if (fread(&value, sizeof(value), 1, file) != 1) return 0;
    return value;
}

float readF32(FILE* file) {
    float value = 0.0f;
    if (fread(&value, sizeof(value), 1, file) != 1) return 0.0f;
    return value;
}

void readString(FILE* file, char* buffer, int size) {
    if (!buffer || size <= 0) return;
    fread(buffer, 1, size, file);
    buffer[size] = '\0';
}

bool readTextureHeader(FILE* file, char magic[9], uint32_t& wallpaper_format, uint32_t& flags,
                       uint32_t& image_width, uint32_t& image_height, char container_magic[9],
                       uint32_t& image_count) {
    readString(file, magic, 8);
    fseek(file, 1, SEEK_CUR);
    fseek(file, 8, SEEK_CUR);  // TEXI0001
    fseek(file, 1, SEEK_CUR);

    if (strncmp(magic, "TEXV", 4) != 0) return false;

    wallpaper_format = readU32(file);
    flags = readU32(file);
    readU32(file);  // allocated width
    readU32(file);  // allocated height
    image_width = readU32(file);
    image_height = readU32(file);
    readU32(file);  // reserved

    readString(file, container_magic, 8);
    fseek(file, 1, SEEK_CUR);
    image_count = readU32(file);

    if (strcmp(container_magic, "TEXB0003") == 0) {
        readU32(file);  // embedded/free-image format
    } else if (strcmp(container_magic, "TEXB0004") == 0) {
        readU32(file);  // embedded/free-image format
        readU32(file);  // video marker
    }
    return true;
}

bool skipMipmap(FILE* file, const char* container_magic) {
    if (strcmp(container_magic, "TEXB0004") == 0) {
        readU32(file);
        readU32(file);
        int c = 0;
        do {
            c = fgetc(file);
            if (c == EOF) return false;
        } while (c != 0);
        readU32(file);
    }

    readU32(file);  // mip width
    readU32(file);  // mip height
    if (strcmp(container_magic, "TEXB0001") != 0) {
        readU32(file);  // LZ4 flag
        readU32(file);  // decompressed size
    }
    const uint32_t data_size = readU32(file);
    return fseek(file, (long)data_size, SEEK_CUR) == 0;
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

TextureMetadata inspectTextureMetadata(const char* path) {
    TextureMetadata metadata;
    if (!path) return metadata;

    const char* extension = strrchr(path, '.');
    if (!extension || strcmp(extension, ".tex") != 0) {
        int width = 0;
        int height = 0;
        int channels = 0;
        if (stbi_info(path, &width, &height, &channels)) {
            metadata.valid = true;
            metadata.width = (uint32_t)width;
            metadata.height = (uint32_t)height;
            metadata.image_count = 1;
        }
        return metadata;
    }

    FILE* file = fopen(path, "rb");
    if (!file) return metadata;

    char magic[9] = {};
    char container_magic[9] = {};
    uint32_t wallpaper_format = 0;
    uint32_t flags = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t image_count = 0;
    if (!readTextureHeader(file, magic, wallpaper_format, flags, width, height, container_magic, image_count)) {
        fclose(file);
        return metadata;
    }
    (void)wallpaper_format;

    metadata.valid = true;
    metadata.width = width;
    metadata.height = height;
    metadata.flags = flags;
    metadata.image_count = image_count;

    for (uint32_t image_number = 0; image_number < image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            if (!skipMipmap(file, container_magic)) {
                fclose(file);
                return metadata;
            }
        }
    }

    if ((flags & kTextureFlagAnimated) == 0) {
        fclose(file);
        return metadata;
    }

    char animation_magic[9] = {};
    readString(file, animation_magic, 8);
    fseek(file, 1, SEEK_CUR);
    if (strncmp(animation_magic, "TEXS000", 7) != 0) {
        fclose(file);
        return metadata;
    }

    const uint32_t frame_count = readU32(file);
    if (strcmp(animation_magic, "TEXS0003") == 0) {
        readU32(file);  // GIF width
        readU32(file);  // GIF height
    }

    float first_frame_width = 0.0f;
    float first_frame_height = 0.0f;
    float total_duration = 0.0f;
    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        readU32(file);  // image/frame number
        total_duration += readF32(file);
        if (strcmp(animation_magic, "TEXS0001") == 0) {
            readU32(file);  // x
            readU32(file);  // y
            const float frame_width = (float)readU32(file);
            readU32(file);
            readU32(file);
            const float frame_height = (float)readU32(file);
            if (frame == 0) {
                first_frame_width = frame_width;
                first_frame_height = frame_height;
            }
        } else {
            readF32(file);  // x
            readF32(file);  // y
            const float frame_width = readF32(file);
            readF32(file);  // width2
            readF32(file);  // height2
            const float frame_height = readF32(file);
            if (frame == 0) {
                first_frame_width = frame_width;
                first_frame_height = frame_height;
            }
        }
    }

    if (frame_count > 0 && first_frame_width > 0.0f && first_frame_height > 0.0f && width > 0 && height > 0) {
        const uint32_t cols = (uint32_t)lround((double)width / first_frame_width);
        const uint32_t rows = (uint32_t)lround((double)height / first_frame_height);
        if (cols > 0 && rows > 0 && cols * rows >= frame_count) {
            metadata.spritesheet_cols = cols;
            metadata.spritesheet_rows = rows;
            metadata.spritesheet_frames = frame_count;
            metadata.spritesheet_duration = total_duration;
        }
    }

    fclose(file);
    return metadata;
}

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
    char container_magic[9] = {};
    uint32_t wallpaper_format = 0;
    uint32_t flags = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    uint32_t image_count = 0;
    if (!readTextureHeader(file, magic, wallpaper_format, flags, image_width, image_height, container_magic,
                           image_count)) {
        LOG_TAG_E(TAG, "Invalid .tex magic in %s", path);
        fclose(file);
        return image;
    }
    (void)flags;

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
    LOG_TAG_D(TAG, "  Format: %s (wp:%u), Size: %ux%u, Container: %s", format_name, wallpaper_format, image_width,
              image_height, container_magic);

    for (uint32_t image_number = 0; image_number < image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            if (strcmp(container_magic, "TEXB0004") == 0) {
                readU32(file);
                readU32(file);
                int c = 0;
                do {
                    c = fgetc(file);
                    if (c == EOF) {
                        fclose(file);
                        return {};
                    }
                } while (c != 0);
                readU32(file);
            }

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
                stbi_uc* pixels =
                    stbi_load_from_memory(data_block.data(), (int)data_block.size(), &width, &height, &channels, 4);
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
