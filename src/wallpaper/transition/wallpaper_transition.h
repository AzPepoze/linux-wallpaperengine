#ifndef WALLPAPER_TRANSITION_H
#define WALLPAPER_TRANSITION_H

#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"
#include "wallpaper/transition/transition_shader.h"

struct EngineContext;

// Holds the outgoing wallpaper's last composed frame and fades it over the
// incoming wallpaper. Phase 2 uses a plain alpha fade; Task 11 swaps in
// Wallpaper Engine's transition shader behind the same interface.
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
    // Freeze the snapshot on screen (used when a switch fails to load).
    void hold() {
        active_ = true;
        progress_ = 0.0f;
        hold_ = true;
    }
    // Draws the snapshot over the currently bound target.
    void composite(EngineContext& ctx);
    void cancel();
    void shutdown();

   private:
    bool ensureTarget(int width, int height);
    void destroyTarget();

    GfxImage image_;
    GfxView texture_view_;
    GfxView attachment_view_;
    int width_ = 0;
    int height_ = 0;
    float elapsed_ = 0.0f;
    float progress_ = 0.0f;
    bool active_ = false;
    bool hold_ = false;
    TransitionConfig config_ = {};
};

#endif  // WALLPAPER_TRANSITION_H
