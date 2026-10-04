#ifndef WALLPAPER_ENGINE_THUMBNAIL_COLORS_H
#define WALLPAPER_ENGINE_THUMBNAIL_COLORS_H

#include <cstdint>

#include "shared/media/media_session.h"

namespace wallpaper_engine {

// Extracts a primary/secondary/tertiary palette and text colors from raw RGBA.
ThumbnailColors extractThumbnailColors(const uint8_t* rgba, int width, int height);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_THUMBNAIL_COLORS_H
