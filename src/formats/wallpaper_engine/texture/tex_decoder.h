#ifndef WALLPAPER_ENGINE_TEX_DECODER_H
#define WALLPAPER_ENGINE_TEX_DECODER_H

#include "decoded_image.h"

namespace wallpaper_engine {

DecodedImage decodeTexture(const char* path, int image_index = 0);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_TEX_DECODER_H
