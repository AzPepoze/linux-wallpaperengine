#ifndef WALLPAPER_ENGINE_TEX_DECODER_H
#define WALLPAPER_ENGINE_TEX_DECODER_H

#include <stddef.h>
#include <stdint.h>

#include "decoded_image.h"

namespace wallpaper_engine {

struct TextureAnimationFrame {
    uint32_t image_index = 0;
    float duration = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct TextureMetadata {
    bool valid = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t flags = 0;
    uint32_t image_count = 0;
    uint32_t spritesheet_cols = 0;
    uint32_t spritesheet_rows = 0;
    uint32_t spritesheet_frames = 0;
    float spritesheet_duration = 0.0f;
    std::vector<TextureAnimationFrame> animation_frames;
};

DecodedImage decodeTexture(const char* path, int image_index = 0);
TextureMetadata inspectTextureMetadata(const char* path);
const TextureAnimationFrame* textureFrameAtTime(const TextureMetadata& metadata, float seconds);

// True when a raw .tex data block begins with an ISO-BMFF 'ftyp' box (embedded MP4 video).
bool isVideoContainer(const uint8_t* data, size_t size);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_TEX_DECODER_H
