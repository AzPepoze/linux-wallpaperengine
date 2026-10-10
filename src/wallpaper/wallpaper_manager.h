#ifndef WALLPAPER_MANAGER_H
#define WALLPAPER_MANAGER_H

#include <future>
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
#include "wallpaper/wallpaper_instance.h"

class ControlServer;
struct SharedAssets;

class WallpaperManager {
   public:
    WallpaperManager() = default;
    ~WallpaperManager() = default;

    // Process-wide assets every instance shares (install/internal providers).
    void setSharedAssets(SharedAssets* shared) {
        shared_assets_ = shared;
    }

    bool load(const std::string& scene_directory, EngineContext& ctx);
    void update(float dt, EngineContext& ctx);
    void render(EngineContext& ctx);
    void onResize(float width, float height);
    void handleInput(const sapp_event* event, EngineContext& ctx);
    void pause();
    void resume();
    void clear(EngineContext& ctx);

    // Runtime wallpaper switching over the control socket.
    void setControlServer(ControlServer* server) {
        control_ = server;
    }
    void pollControl(EngineContext& ctx);
    void applyAudioRequest(EngineContext& ctx, const SwitchRequest& request);
    bool hasPendingSwitch() const {
        return pending_switch_.has_value();
    }
    bool takePendingSwitch(SwitchRequest& out);

    // Starts or polls preparation and commits at readiness; true when a request was accepted.
    bool beginPendingSwitch(EngineContext& ctx);
    bool isTransitioning() const {
        return transition_.active();
    }
    void updateTransition(float dt);
    // Continue mode: the outgoing instance renders offscreen to refresh the live source.
    bool stepOutgoingForTransition(EngineContext& ctx, float dt);
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
        return active_instance_ ? active_instance_->wallpaper.get() : nullptr;
    }
    bool hasActiveWallpaper() const {
        return active_instance_ != nullptr;
    }

   private:
    // Activates the instance first so its layers tear down through its own view.
    void destroyInstance(EngineContext& ctx, std::unique_ptr<WallpaperInstance>& instance);
    struct LoadJob;
    void pollLoad(EngineContext& ctx);
    void commitLoad(EngineContext& ctx);
    std::shared_ptr<LoadJob> load_job_;
    std::vector<std::shared_ptr<LoadJob>> retired_jobs_;

    // Pumps the outgoing instance's video/audio without rendering it (freeze).
    void tickOutgoingAudio(EngineContext& ctx, float dt);

    std::unique_ptr<WallpaperInstance> active_instance_;
    std::unique_ptr<WallpaperInstance> outgoing_instance_;
    WallpaperInstance* active_view_ = nullptr;
    SharedAssets* shared_assets_ = nullptr;
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
