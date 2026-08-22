#ifndef WEB_WALLPAPER_H
#define WEB_WALLPAPER_H

#include "wallpaper/wallpaper.h"

class WebWallpaper : public Wallpaper {
   public:
    explicit WebWallpaper(EngineContext& ctx) {
        (void)ctx;
    }
    ~WebWallpaper() override = default;

    WallpaperType getType() const override {
        return WallpaperType::Web;
    }
    bool load(const std::string& path, EngineContext& ctx) override {
        (void)path;
        (void)ctx;
        return true;
    }
    void update(float dt, EngineContext& ctx) override {
        (void)dt;
        (void)ctx;
    }
    void render(EngineContext& ctx) override {
        (void)ctx;
    }
};

#endif  // WEB_WALLPAPER_H
