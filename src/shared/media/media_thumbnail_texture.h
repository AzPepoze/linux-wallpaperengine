#ifndef WALLPAPER_ENGINE_MEDIA_THUMBNAIL_TEXTURE_H
#define WALLPAPER_ENGINE_MEDIA_THUMBNAIL_TEXTURE_H

#include <cstdint>
#include <vector>

#include "shared/media/media_session.h"
#include "sokol_gfx.h"

namespace wallpaper_engine {

// The `$mediaThumbnail` system texture: the current track's album art, scaled to a fixed square. Every material that
// samples it owns one image (the pass destroys it with the material); all of them are refreshed together.
class MediaThumbnailTexture {
   public:
    static constexpr int kSize = 256;

    static MediaThumbnailTexture& instance();

    sg_image create(bool previous = false);
    // Keeps the newest thumbnail; it reaches the images on the next flush().
    void setThumbnail(const ThumbnailColors& thumbnail);
    // Uploads pending changes. Call once per frame from the render thread.
    void flush();
    bool inUse();

   private:
    static MediaThumbnailTexture& previous();
    void prune();

    std::vector<uint32_t> images_;
    std::vector<uint32_t> dirty_;
    std::vector<uint8_t> pixels_;  // kSize x kSize RGBA, transparent until a thumbnail arrives
    bool pixels_changed_ = false;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MEDIA_THUMBNAIL_TEXTURE_H
