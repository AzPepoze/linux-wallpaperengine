#ifndef WALLPAPER_ENGINE_TEX_DECODER_H
#define WALLPAPER_ENGINE_TEX_DECODER_H

#include <stdint.h>

#include "decoded_image.h"

namespace wallpaper_engine {

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
};

DecodedImage decodeTexture(const char* path, int image_index = 0);
TextureMetadata inspectTextureMetadata(const char* path);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_TEX_DECODER_H
