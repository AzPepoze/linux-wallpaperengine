#ifndef SCENE_2D_WALLPAPER_H
#define SCENE_2D_WALLPAPER_H

#include <chrono>
#include <memory>

#include "shared/graphics/render.h"
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
    void beginLoadDocument(wallpaper_engine::SceneDocument document, EngineContext& ctx);
    void beginPrewarmLoaded(EngineContext& ctx);
    bool stepLoad(std::chrono::milliseconds budget, EngineContext& ctx);
    bool loadingComplete() const {
        return load_complete_;
    }
    bool loadingFailed() const {
        return load_failed_;
    }
    bool stepCleanup(std::chrono::milliseconds budget);
    void update(float dt, EngineContext& ctx) override;
    void render(EngineContext& ctx) override;
    void onResize(float width, float height) override;
    void handleInput(const sapp_event* event, EngineContext& ctx) override;
    void clear() override;

    Scene2DRuntime* getRuntime() {
        return runtime_.get();
    }

   protected:
    bool applyParsedScene(ParsedScene parsed, EngineContext& ctx, bool precompile_blends = true);
    void startPrewarm(EngineContext& ctx);
    std::unique_ptr<Scene2DRuntime> runtime_;
    std::unique_ptr<SceneBuildJob> build_job_;
    bool load_complete_ = false;
    bool load_failed_ = false;
    bool cleanup_done_ = false;
    bool load_applied_ = false;
    int prewarm_width_ = 0;
    int prewarm_height_ = 0;
    bool blend_precompile_done_ = true;
    BlendPipelinePrecompileJobHandle blend_precompile_job_;
};

#endif  // SCENE_2D_WALLPAPER_H
