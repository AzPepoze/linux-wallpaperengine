#ifndef WALLPAPER_ENGINE_TEX_DECODER_INTERNAL_H
#define WALLPAPER_ENGINE_TEX_DECODER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <cstdio>
#include <vector>

#include "decoded_image.h"
#include "shared/core/vfs.h"

namespace wallpaper_engine {
namespace tex_internal {
constexpr uint32_t kTextureFlagAnimated = 4;
constexpr uint32_t kTextureFlagVideoMask = 0x22;

enum class TexFormat : uint32_t {
    RGBA8 = 0,
    BC1_DXT1 = 1,
    BC2_DXT3 = 2,
    BC3_DXT5 = 4,
    BC2_DXT3_ALT = 6,
    BC1_DXT1_ALT = 7,
    RG8 = 8,
    R8 = 9,
};

struct FormatInfo {
    const char* name;
    PixelFormat pixel_format;
    uint32_t channels;
    size_t bytes_per_pixel;  // 0 for block-compressed
};

struct ScopedFile {
    FILE* handle = nullptr;

    explicit ScopedFile(const char* path) : handle(vfs::open(path)) {}
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

struct TexHeader {
    char version_magic[9] = {};
    char container_magic[9] = {};
    uint32_t format_id = 0;
    uint32_t flags = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    uint32_t image_count = 0;
};

FormatInfo getFormatInfo(uint32_t format_id);
size_t getBlockCompressedBlockSize(PixelFormat format);
size_t expectedPixelDataSize(uint32_t width, uint32_t height, PixelFormat format);

uint32_t readU32(FILE* file);
float readF32(FILE* file);
void readFixedString(FILE* file, char* buffer, int length);
bool readTextureHeader(FILE* file, TexHeader& header);
bool skipMipmap(FILE* file, const char* container_magic, uint32_t flags = 0);

bool tryDecodeEmbeddedImage(const uint8_t* data, size_t size, DecodedImage& out_image);
bool decompressLz4Payload(const uint8_t* compressed_data, size_t compressed_size, uint32_t decompressed_size,
                          std::vector<uint8_t>& out_decompressed);
std::vector<uint8_t> unpadPaddedRows(const uint8_t* src, uint32_t img_w, uint32_t img_h, uint32_t mip_w, size_t bpp);
std::vector<uint8_t> unpadBlockCompressedRows(const uint8_t* src, uint32_t img_w, uint32_t img_h, uint32_t mip_w,
                                              size_t block_size);
DecodedImage decodeStandardImage(const char* path, int image_index);
}  // namespace tex_internal
}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_TEX_DECODER_INTERNAL_H
