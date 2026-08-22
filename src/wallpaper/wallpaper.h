#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <string>

#include "shared/core/engine_context.h"
#include "sokol_app.h"

enum class WallpaperType { Scene2D, Scene3D, Video, Web };

class Wallpaper {
   public:
    virtual ~Wallpaper() = default;

    virtual WallpaperType getType() const = 0;
    virtual bool load(const std::string& path, EngineContext& ctx) = 0;
    virtual void update(float dt, EngineContext& ctx) = 0;
    virtual void render(EngineContext& ctx) = 0;
    virtual void onResize(float, float) {}
    virtual void handleInput(const sapp_event*, EngineContext&) {}
    virtual void pause() {}
    virtual void resume() {}
    virtual void clear() {}
};

#endif  // WALLPAPER_H
