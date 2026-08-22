#ifndef WALLPAPER_MANAGER_H
#define WALLPAPER_MANAGER_H

#include <memory>
#include <string>

#include "shared/core/engine_context.h"
#include "sokol_app.h"
#include "wallpaper/wallpaper.h"

class WallpaperManager {
   public:
    WallpaperManager() = default;
    ~WallpaperManager() = default;

    bool load(const std::string& scene_directory, EngineContext& ctx);
    void update(float dt, EngineContext& ctx);
    void render(EngineContext& ctx);
    void onResize(float width, float height);
    void handleInput(const sapp_event* event, EngineContext& ctx);
    void clear();

    Wallpaper* getActiveWallpaper() const {
        return active_wallpaper_.get();
    }
    bool hasActiveWallpaper() const {
        return active_wallpaper_ != nullptr;
    }

    static bool isVideoFile(const char* path);
    static bool isHtmlFile(const char* path);

   private:
    std::unique_ptr<Wallpaper> active_wallpaper_;
};

#endif  // WALLPAPER_MANAGER_H
