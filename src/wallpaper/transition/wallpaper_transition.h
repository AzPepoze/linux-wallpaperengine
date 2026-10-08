#ifndef WALLPAPER_TRANSITION_H
#define WALLPAPER_TRANSITION_H

#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"
#include "wallpaper/transition/transition_shader.h"

struct EngineContext;

// Holds the outgoing wallpaper's last frame and fades it over the incoming one.
class WallpaperTransition {
   public:
    WallpaperTransition() = default;
    ~WallpaperTransition() = default;

    // Copies `source` into the snapshot and starts the transition.
    bool begin(EngineContext& ctx, sg_view source, sg_image source_image, int width, int height,
               const TransitionConfig& config);
    void update(float dt);
    bool active() const {
        return active_;
    }
    float progress() const {
        return progress_;
    }
    // Continue mode; begin() must already have sized the snapshot.
    void setLive(bool live) {
        live_ = live;
    }
    bool live() const {
        return live_;
    }
    // Re-blits the outgoing frame into the overlay; no-op unless active and live.
    void updateSource(EngineContext& ctx, sg_view source, sg_image source_image, int width, int height);
    // Freeze the snapshot on screen (used when a switch fails to load).
    void hold() {
        active_ = true;
        progress_ = 0.0f;
        hold_ = true;
    }
    void startHeld(const TransitionConfig& config) {
        config_ = config;
        elapsed_ = 0.0f;
        progress_ = 0.0f;
        hold_ = false;
        live_ = false;
    }
    void composite(EngineContext& ctx);
    // Registers the overlay as a diagnostic scene stage; call before the swapchain pass.
    void captureStage(EngineContext& ctx);
    void cancel();
    void shutdown();

   private:
    bool ensureTarget(int width, int height);
    void destroyTarget();
    void copySource(EngineContext& ctx, sg_view source, sg_image source_image, int width, int height);
    void drawOverlay(EngineContext& ctx, int width, int height);

    GfxImage image_;
    GfxView texture_view_;
    GfxView attachment_view_;
    TransitionShader shader_;
    int width_ = 0;
    int height_ = 0;
    float elapsed_ = 0.0f;
    float progress_ = 0.0f;
    bool active_ = false;
    bool hold_ = false;
    bool live_ = false;
    uint32_t switch_seed_ = 0;
    TransitionConfig config_ = {};
};

#endif  // WALLPAPER_TRANSITION_H
