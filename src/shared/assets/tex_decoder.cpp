#include "tex_decoder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "lz4.h"
#include "shared/core/logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TAG "TEXTURE"

namespace wallpaper_engine {

namespace {

// ============================================================================
// Constants & Formats
// ============================================================================

constexpr uint32_t kTextureFlagAnimated = 4;
// Bit 1 (0x02) and bit 5 (0x20) are documented as video-embedded in Wallpaper Engine source.
constexpr uint32_t kTextureFlagVideoMask = 0x22;

enum class TexFormat : uint32_t {
    RGBA8 = 0,
    BC1_DXT1 = 4,
    BC2_DXT3 = 5,
    BC3_DXT5 = 6,
    RG8 = 8,
    R8 = 9,
};

struct FormatInfo {
    const char* name;
    PixelFormat pixel_format;
    uint32_t channels;
    size_t bytes_per_pixel;  // 0 for block-compressed
};

FormatInfo getFormatInfo(uint32_t format_id) {
    switch (static_cast<TexFormat>(format_id)) {
        case TexFormat::RGBA8:
            return {"RGBA8", PixelFormat::RGBA8, 4, 4};
        case TexFormat::BC1_DXT1:
            return {"DXT1/BC1", PixelFormat::BC1, 4, 0};
        case TexFormat::BC2_DXT3:
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

// ============================================================================
// RAII File Reader & Binary Helpers
// ============================================================================

struct ScopedFile {
    FILE* handle = nullptr;

    explicit ScopedFile(const char* path, const char* mode = "rb") : handle(std::fopen(path, mode)) {}
    ~ScopedFile() {
        if (handle) std::fclose(handle);
    }

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    bool isOpen() const {
        return handle != nullptr;
    }
    FILE* get() const {
        return handle;
    }
    operator FILE*() const {
        return handle;
    }
};

uint32_t readU32(FILE* file) {
    uint32_t value = 0;
    return (std::fread(&value, sizeof(value), 1, file) == 1) ? value : 0;
}

float readF32(FILE* file) {
    float value = 0.0f;
    return (std::fread(&value, sizeof(value), 1, file) == 1) ? value : 0.0f;
}

void readFixedString(FILE* file, char* buffer, int length) {
    if (!buffer || length <= 0) return;
    std::fread(buffer, 1, length, file);
    buffer[length] = '\0';
}

// ============================================================================
// Texture Header & Mip Parsing
// ============================================================================

struct TexHeader {
    char version_magic[9] = {};
    char container_magic[9] = {};
    uint32_t format_id = 0;
    uint32_t flags = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    uint32_t image_count = 0;
};

bool readTextureHeader(FILE* file, TexHeader& header) {
    readFixedString(file, header.version_magic, 8);
    std::fseek(file, 1, SEEK_CUR);  // Null delimiter
    std::fseek(file, 8, SEEK_CUR);  // TEXI0001
    std::fseek(file, 1, SEEK_CUR);  // Null delimiter

    if (std::strncmp(header.version_magic, "TEXV", 4) != 0) return false;

    header.format_id = readU32(file);
    header.flags = readU32(file);
    readU32(file);  // allocated width
    readU32(file);  // allocated height
    header.image_width = readU32(file);
    header.image_height = readU32(file);
    readU32(file);  // reserved

    readFixedString(file, header.container_magic, 8);
    std::fseek(file, 1, SEEK_CUR);
    header.image_count = readU32(file);

    if (std::strcmp(header.container_magic, "TEXB0003") == 0) {
        readU32(file);  // embedded/free-image format
    } else if (std::strcmp(header.container_magic, "TEXB0004") == 0) {
        readU32(file);  // embedded/free-image format
        readU32(file);  // video marker
    }
    return true;
}

bool skipMipmap(FILE* file, const char* container_magic) {
    if (std::strcmp(container_magic, "TEXB0004") == 0) {
        readU32(file);
        readU32(file);
        int c = 0;
        do {
            c = std::fgetc(file);
            if (c == EOF) return false;
        } while (c != 0);
        readU32(file);
    }

    readU32(file);  // mip width
    readU32(file);  // mip height
    if (std::strcmp(container_magic, "TEXB0001") != 0) {
        readU32(file);  // LZ4 flag
        readU32(file);  // decompressed size
    }
    const uint32_t data_size = readU32(file);
    return std::fseek(file, static_cast<long>(data_size), SEEK_CUR) == 0;
}

// ============================================================================
// Payload Decoding & Decompression Helpers
// ============================================================================

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

}  // namespace

// ============================================================================
// Public API: inspectTextureMetadata
// ============================================================================

TextureMetadata inspectTextureMetadata(const char* path) {
    TextureMetadata metadata;
    if (!path) return metadata;

    const char* extension = std::strrchr(path, '.');
    if (!extension || std::strcmp(extension, ".tex") != 0) {
        int width = 0, height = 0, channels = 0;
        if (stbi_info(path, &width, &height, &channels)) {
            metadata.valid = true;
            metadata.width = static_cast<uint32_t>(width);
            metadata.height = static_cast<uint32_t>(height);
            metadata.image_count = 1;
        }
        return metadata;
    }

    ScopedFile file(path);
    if (!file.isOpen()) return metadata;

    TexHeader header;
    if (!readTextureHeader(file, header)) return metadata;

    metadata.valid = true;
    metadata.width = header.image_width;
    metadata.height = header.image_height;
    metadata.flags = header.flags;
    metadata.image_count = header.image_count;

    // TEXB0004 embeds raw video (MP4/H.264) in the mip payload.
    // Attempting to skip those mips with the normal reader produces corrupt reads.
    // Return valid metadata (so the asset manager knows size/count) but skip mip parsing;
    // the caller will route the file to VideoTexture.
    if (std::strcmp(header.container_magic, "TEXB0004") == 0 ||
        (header.flags & kTextureFlagVideoMask) != 0) {
        return metadata;
    }

    for (uint32_t image_number = 0; image_number < header.image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            if (!skipMipmap(file, header.container_magic)) return metadata;
        }
    }

    if ((header.flags & kTextureFlagAnimated) == 0) return metadata;

    char animation_magic[9] = {};
    readFixedString(file, animation_magic, 8);
    std::fseek(file, 1, SEEK_CUR);
    if (std::strncmp(animation_magic, "TEXS000", 7) != 0) return metadata;

    const uint32_t frame_count = readU32(file);
    if (std::strcmp(animation_magic, "TEXS0003") == 0) {
        readU32(file);  // GIF width
        readU32(file);  // GIF height
    }

    float first_frame_width = 0.0f;
    float first_frame_height = 0.0f;
    float total_duration = 0.0f;
    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        readU32(file);  // image/frame number
        total_duration += readF32(file);
        if (std::strcmp(animation_magic, "TEXS0001") == 0) {
            readU32(file);  // x
            readU32(file);  // y
            const float frame_width = static_cast<float>(readU32(file));
            readU32(file);
            readU32(file);
            const float frame_height = static_cast<float>(readU32(file));
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

    if (frame_count > 0 && first_frame_width > 0.0f && first_frame_height > 0.0f && header.image_width > 0 &&
        header.image_height > 0) {
        const uint32_t cols =
            static_cast<uint32_t>(std::lround(static_cast<double>(header.image_width) / first_frame_width));
        const uint32_t rows =
            static_cast<uint32_t>(std::lround(static_cast<double>(header.image_height) / first_frame_height));
        if (cols > 0 && rows > 0 && cols * rows >= frame_count) {
            metadata.spritesheet_cols = cols;
            metadata.spritesheet_rows = rows;
            metadata.spritesheet_frames = frame_count;
            metadata.spritesheet_duration = total_duration;
        }
    }

    return metadata;
}

// ============================================================================
// Public API: decodeTexture
// ============================================================================

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

    // TEXB0004 contains an embedded video stream, not raw pixel data.
    // Return an invalid image so AssetManager routes this path to VideoTexture::open.
    if (std::strcmp(header.container_magic, "TEXB0004") == 0 ||
        (header.flags & kTextureFlagVideoMask) != 0) {
        LOG_TAG_I(TAG, "Detected video-embedded .tex (%s), handing off to VideoTexture: %s",
                  header.container_magic, path);
        return {};
    }

    for (uint32_t image_number = 0; image_number < header.image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            if (std::strcmp(header.container_magic, "TEXB0004") == 0) {
                readU32(file);
                readU32(file);
                int c = 0;
                do {
                    c = std::fgetc(file);
                    if (c == EOF) return {};
                } while (c != 0);
                readU32(file);
            }

            const uint32_t mip_width = readU32(file);
            const uint32_t mip_height = readU32(file);

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

            // Case 1: Embedded container (PNG / JPEG / WebP / etc.)
            DecodedImage image;
            if (tryDecodeEmbeddedImage(data_block.data(), data_block.size(), image)) {
                return image;
            }

            // Case 2: Raw / Compressed payload
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

            // Handle power-of-two GPU stride padding if applicable
            const size_t bpp = format.bytes_per_pixel;
            const bool is_padded = (bpp > 0) &&
                                   (mip_width != header.image_width || mip_height != header.image_height) &&
                                   (raw_data.size() == static_cast<size_t>(mip_width) * mip_height * bpp) &&
                                   (header.image_width <= mip_width) && (header.image_height <= mip_height);

            if (is_padded) {
                image.pixels =
                    unpadPaddedRows(raw_data.data(), header.image_width, header.image_height, mip_width, bpp);
            } else if (raw_data.size() == expectedPixelDataSize(mip_width, mip_height, image.format)) {
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
