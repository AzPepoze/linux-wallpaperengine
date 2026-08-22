#ifndef SCENE_2D_WALLPAPER_H
#define SCENE_2D_WALLPAPER_H

#include <memory>

#include "wallpaper/2d/scene_2d.h"
#include "wallpaper/2d/scene_builder.h"
#include "wallpaper/wallpaper.h"

class Scene2DWallpaper : public Wallpaper {
   public:
    explicit Scene2DWallpaper(EngineContext& ctx);
    ~Scene2DWallpaper() override;

    WallpaperType getType() const override {
        return WallpaperType::Scene2D;
    }
    bool load(const std::string& path, EngineContext& ctx) override;
    void update(float dt, EngineContext& ctx) override;
    void render(EngineContext& ctx) override;
    void onResize(float width, float height) override;
    void handleInput(const sapp_event* event, EngineContext& ctx) override;
    void clear() override;

    Scene2DRuntime* getRuntime() {
        return runtime_.get();
    }

   protected:
    bool applyParsedScene(ParsedScene parsed, EngineContext& ctx);
    std::unique_ptr<Scene2DRuntime> runtime_;
};

#endif  // SCENE_2D_WALLPAPER_H
