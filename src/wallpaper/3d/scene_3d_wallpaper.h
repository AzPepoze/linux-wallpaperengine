#ifndef SCENE_3D_WALLPAPER_H
#define SCENE_3D_WALLPAPER_H

#include "wallpaper/wallpaper.h"

class Scene3DWallpaper : public Wallpaper {
   public:
    explicit Scene3DWallpaper(EngineContext& ctx) {
        (void)ctx;
    }
    ~Scene3DWallpaper() override = default;

    WallpaperType getType() const override {
        return WallpaperType::Scene3D;
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

#endif  // SCENE_3D_WALLPAPER_H
