#include <cmath>
#include <cstring>

#include "shared/core/logger.h"
#include "stb_image.h"
#include "tex_decoder.h"
#include "tex_decoder_internal.h"

#define TAG "TEXTURE"

namespace wallpaper_engine {
using namespace tex_internal;

namespace tex_internal {
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

bool readTextureHeader(FILE* file, TexHeader& header) {
    readFixedString(file, header.version_magic, 8);
    std::fseek(file, 1, SEEK_CUR);  // Null delimiter
    std::fseek(file, 8, SEEK_CUR);  // TEXI0001
    std::fseek(file, 1, SEEK_CUR);  // Null delimiter

    if (std::strncmp(header.version_magic, "TEXV", 4) != 0) return false;

    header.format_id = readU32(file);
    header.flags = readU32(file);
    readU32(file);
    readU32(file);
    header.image_width = readU32(file);
    header.image_height = readU32(file);
    if ((header.flags & 0x40) != 0) {
        readU32(file);  // image depth / 3D slice count
    }
    readU32(file);

    readFixedString(file, header.container_magic, 8);
    std::fseek(file, 1, SEEK_CUR);
    header.image_count = readU32(file);

    if (std::strcmp(header.container_magic, "TEXB0003") == 0) {
        readU32(file);
    } else if (std::strcmp(header.container_magic, "TEXB0004") == 0) {
        readU32(file);
        readU32(file);  // video marker
    }
    return true;
}

bool skipMipmap(FILE* file, const char* container_magic, uint32_t flags) {
    readU32(file);
    readU32(file);
    if ((flags & 0x40) != 0) {
        readU32(file);
    }
    if (std::strcmp(container_magic, "TEXB0001") != 0) {
        readU32(file);
        readU32(file);
    }
    const uint32_t data_size = readU32(file);
    return std::fseek(file, static_cast<long>(data_size), SEEK_CUR) == 0;
}
}  // namespace tex_internal

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

    for (uint32_t image_number = 0; image_number < header.image_count; ++image_number) {
        const uint32_t mip_count = readU32(file);
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            if (!skipMipmap(file, header.container_magic, header.flags)) return metadata;
        }
    }

    if ((header.flags & kTextureFlagAnimated) == 0) return metadata;

    char animation_magic[9] = {};
    readFixedString(file, animation_magic, 8);
    std::fseek(file, 1, SEEK_CUR);
    if (std::strncmp(animation_magic, "TEXS000", 7) != 0) return metadata;

    const uint32_t frame_count = readU32(file);
    if (frame_count == 0 || frame_count > 1000000) return metadata;
    if (std::strcmp(animation_magic, "TEXS0003") == 0) {
        readU32(file);
        readU32(file);
    }

    float first_frame_width = 0.0f;
    float first_frame_height = 0.0f;
    double total_duration = 0.0;
    std::vector<TextureAnimationFrame> frames;
    frames.reserve(frame_count);
    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        TextureAnimationFrame entry;
        entry.image_index = readU32(file);
        entry.duration = readF32(file);
        total_duration += entry.duration;
        if (std::strcmp(animation_magic, "TEXS0001") == 0) {
            entry.x = (float)readU32(file);
            entry.y = (float)readU32(file);
            const float frame_width = static_cast<float>(readU32(file));
            readU32(file);
            readU32(file);
            const float frame_height = static_cast<float>(readU32(file));
            entry.width = frame_width;
            entry.height = frame_height;
            if (frame == 0) {
                first_frame_width = frame_width;
                first_frame_height = frame_height;
            }
        } else {
            entry.x = readF32(file);
            entry.y = readF32(file);
            const float frame_width = readF32(file);
            readF32(file);
            readF32(file);
            const float frame_height = readF32(file);
            entry.width = frame_width;
            entry.height = frame_height;
            if (frame == 0) {
                first_frame_width = frame_width;
                first_frame_height = frame_height;
            }
        }
        if (frame == 0 && first_frame_width > 0.0f && first_frame_height > 0.0f && std::isfinite(first_frame_width) &&
            std::isfinite(first_frame_height)) {
            const auto cols = (uint32_t)std::lround(header.image_width / first_frame_width);
            const auto rows = (uint32_t)std::lround(header.image_height / first_frame_height);
            // Particle shaders use the uniform grid, since some atlases have inconsistent rectangles.
            if (cols > 0 && rows > 0 && (uint64_t)cols * rows >= frame_count) {
                metadata.spritesheet_cols = cols;
                metadata.spritesheet_rows = rows;
                metadata.spritesheet_frames = frame_count;
            }
        }
        if (entry.image_index >= header.image_count || !std::isfinite(entry.duration) || entry.duration <= 0.0f ||
            !std::isfinite(entry.x) || !std::isfinite(entry.y) || !std::isfinite(entry.width) ||
            !std::isfinite(entry.height) || entry.x < 0.0f || entry.y < 0.0f || entry.width <= 0.0f ||
            entry.height <= 0.0f || entry.x + entry.width > header.image_width + 0.01f ||
            entry.y + entry.height > header.image_height + 0.01f)
            return metadata;
        // Fractional grids can overshoot the edge by a few thousandths of a pixel.
        entry.width = std::min(entry.width, (float)header.image_width - entry.x);
        entry.height = std::min(entry.height, (float)header.image_height - entry.y);
        frames.push_back(entry);
    }
    metadata.animation_frames = std::move(frames);
    metadata.spritesheet_duration = total_duration;

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

const TextureAnimationFrame* textureFrameAtTime(const TextureMetadata& metadata, float seconds) {
    if (metadata.animation_frames.empty() || metadata.spritesheet_duration <= 0.0f || !std::isfinite(seconds))
        return nullptr;
    // Double precision keeps frame durations from drifting over long animations.
    double duration = 0.0;
    for (const auto& frame : metadata.animation_frames) duration += frame.duration;
    double phase = std::fmod((double)seconds, duration);
    if (phase < 0.0) phase += duration;
    for (const auto& frame : metadata.animation_frames) {
        if (phase < frame.duration) return &frame;
        phase -= frame.duration;
    }
    return &metadata.animation_frames.back();
}

}  // namespace wallpaper_engine
