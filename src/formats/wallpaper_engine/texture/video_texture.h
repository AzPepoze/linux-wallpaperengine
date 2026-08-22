#ifndef WALLPAPER_ENGINE_VIDEO_TEXTURE_H
#define WALLPAPER_ENGINE_VIDEO_TEXTURE_H

#include <cstdint>
#include <memory>
#include <vector>

namespace wallpaper_engine {

class VideoTexture {
   public:
    ~VideoTexture();
    VideoTexture(const VideoTexture&) = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;

    static std::unique_ptr<VideoTexture> open(const char* texture_path);
    uint32_t width() const;
    uint32_t height() const;
    float frameDuration() const;
    bool decodeNextFrame(std::vector<uint8_t>& rgba_pixels);

   private:
    VideoTexture() = default;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_VIDEO_TEXTURE_H
