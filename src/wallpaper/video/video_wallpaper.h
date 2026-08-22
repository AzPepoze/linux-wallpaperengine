#ifndef VIDEO_WALLPAPER_H
#define VIDEO_WALLPAPER_H

#include "wallpaper/2d/scene_2d_wallpaper.h"

class VideoWallpaper : public Scene2DWallpaper {
   public:
    explicit VideoWallpaper(EngineContext& ctx) : Scene2DWallpaper(ctx) {}
    ~VideoWallpaper() override = default;

    WallpaperType getType() const override {
        return WallpaperType::Video;
    }
    bool load(const std::string& path, EngineContext& ctx) override;
};

#endif  // VIDEO_WALLPAPER_H
