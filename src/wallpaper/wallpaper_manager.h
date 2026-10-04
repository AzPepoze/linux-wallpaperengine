#ifndef WALLPAPER_MANAGER_H
#define WALLPAPER_MANAGER_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/control/control_protocol.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/engine_context.h"
#include "sokol_app.h"
#include "wallpaper/transition/wallpaper_transition.h"
#include "wallpaper/wallpaper.h"

class ControlServer;

class WallpaperManager {
   public:
    WallpaperManager() = default;
    ~WallpaperManager() = default;

    bool load(const std::string& scene_directory, EngineContext& ctx);
    void update(float dt, EngineContext& ctx);
    void render(EngineContext& ctx);
    void onResize(float width, float height);
    void handleInput(const sapp_event* event, EngineContext& ctx);
    void pause();
    void resume();
    void clear();

    // Runtime wallpaper switching over the control socket.
    void setControlServer(ControlServer* server) {
        control_ = server;
    }
    void pollControl(EngineContext& ctx);
    bool hasPendingSwitch() const {
        return pending_switch_.has_value();
    }
    bool takePendingSwitch(SwitchRequest& out);

    // Captures the outgoing frame, loads the requested wallpaper and starts the
    // transition. Returns true when a pending switch was processed.
    bool beginPendingSwitch(EngineContext& ctx);
    bool isTransitioning() const {
        return transition_.active();
    }
    void updateTransition(float dt);
    void compositeTransition(EngineContext& ctx) {
        transition_.composite(ctx);
    }
    void captureTransitionStage(EngineContext& ctx) {
        transition_.captureStage(ctx);
    }
    void setTransitionConfig(const TransitionConfig& config) {
        transition_config_ = config;
    }
    void shutdownTransition() {
        transition_.shutdown();
    }

    Wallpaper* getActiveWallpaper() const {
        return active_wallpaper_.get();
    }
    bool hasActiveWallpaper() const {
        return active_wallpaper_ != nullptr;
    }

   private:
    std::unique_ptr<Wallpaper> active_wallpaper_;
    ControlServer* control_ = nullptr;
    std::optional<SwitchRequest> pending_switch_;
    WallpaperTransition transition_;
    TransitionConfig transition_config_ = {};
    uint32_t random_seed_ = 0x5eed1234u;
    AudioEngine::GroupId fading_group_ = AudioEngine::kDefaultGroup;
    AudioEngine::GroupId active_group_ = AudioEngine::kDefaultGroup;
    bool audio_crossfade_ = false;
};

#endif  // WALLPAPER_MANAGER_H
