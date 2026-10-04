#include "wallpaper/wallpaper_manager.h"

#include <chrono>
#include <utility>

#include "app/control/control_server.h"
#include "app/wallpaper_switch.h"
#include "shared/assets/shared_assets.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/transition/transition_audio.h"
#include "wallpaper/wallpaper_loader.h"

bool WallpaperManager::load(const std::string& scene_directory, EngineContext& ctx) {
    if (scene_directory.empty()) return false;

    const ProjectInfo info = ProjectInfo::detect(scene_directory);
    if (!WallpaperLoader::canLoad(info)) return false;

    // Retain the current wallpaper as the outgoing one: it stays alive (and keeps
    // playing) until the transition ends. The initial load has none.
    if (active_instance_) outgoing_instance_ = std::move(active_instance_);

    auto instance = std::make_unique<WallpaperInstance>();
    instance->audio_group = AudioEngine::instance().createGroup();
    LOG_TAG_I("WALLPAPER_MGR", "New instance audio group=%u for %s", (unsigned)instance->audio_group,
              scene_directory.c_str());
    if (shared_assets_) instance->assets.attachShared(shared_assets_);
    instance->state.wallpaper_path = scene_directory;
    instance->state.is_pkg = ctx.is_pkg;

    activateInstance(ctx, *instance, active_view_);
    // The incoming audio starts silent when a frame was already captured for the
    // transition; otherwise it plays at full volume immediately.
    AudioEngine::instance().setGroupVolume(instance->audio_group, transition_.active() ? 0.0f : 1.0f);

    auto wallpaper = WallpaperLoader::load(info, ctx);
    if (!wallpaper) {
        AudioEngine::instance().destroyGroup(instance->audio_group);
        if (outgoing_instance_) {
            activateInstance(ctx, *outgoing_instance_, active_view_);
            active_instance_ = std::move(outgoing_instance_);
        } else {
            deactivateInstance(ctx, active_view_);
        }
        return false;
    }
    instance->wallpaper = std::move(wallpaper);
    active_instance_ = std::move(instance);
    return true;
}

void WallpaperManager::destroyInstance(EngineContext& ctx, std::unique_ptr<WallpaperInstance>& instance) {
    if (!instance) return;
    // Make it the active view so its runtime cleanup tears down its own layers.
    activateInstance(ctx, *instance, active_view_);
    instance->wallpaper.reset();  // ~Wallpaper -> clear() -> Scene2DRuntime::cleanup()
    instance.reset();
    active_view_ = nullptr;  // it pointed into the destroyed instance
    if (active_instance_) activateInstance(ctx, *active_instance_, active_view_);
}

void WallpaperManager::update(float dt, EngineContext& ctx) {
    // A finished (or never-started) fade: drop the outgoing wallpaper and its group.
    if (outgoing_instance_ && !transition_.active()) {
        AudioEngine::instance().destroyGroup(fading_group_);
        fading_group_ = AudioEngine::kDefaultGroup;
        destroyInstance(ctx, outgoing_instance_);
    }

    if (outgoing_instance_ && transition_.active()) tickOutgoingAudio(ctx, dt);
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->update(dt, ctx);
}

void WallpaperManager::tickOutgoingAudio(EngineContext& ctx, float dt) {
    if (!outgoing_instance_) return;
    activateInstance(ctx, *outgoing_instance_, active_view_);
    outgoing_instance_->assets.updateVideoTextures(dt, ctx.scene.layers);
    activateInstance(ctx, *active_instance_, active_view_);
}

void WallpaperManager::render(EngineContext& ctx) {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->render(ctx);
}

void WallpaperManager::onResize(float width, float height) {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->onResize(width, height);
}

void WallpaperManager::handleInput(const sapp_event* event, EngineContext& ctx) {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->handleInput(event, ctx);
}

void WallpaperManager::pause() {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->pause();
}

void WallpaperManager::resume() {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->resume();
}

void WallpaperManager::clear(EngineContext& ctx) {
    AudioEngine::instance().destroyGroup(fading_group_);
    fading_group_ = AudioEngine::kDefaultGroup;
    audio_crossfade_ = false;
    if (outgoing_instance_) destroyInstance(ctx, outgoing_instance_);
    if (active_instance_) {
        LOG_TAG_I("WALLPAPER_MGR", "Clearing active wallpaper instance...");
        active_instance_->wallpaper.reset();
        active_instance_.reset();
        active_view_ = nullptr;
        LOG_TAG_I("WALLPAPER_MGR", "Active wallpaper cleared.");
    }
}

void WallpaperManager::pollControl(EngineContext& ctx) {
    (void)ctx;
    if (!control_) return;
    std::vector<SwitchRequest> requests;
    control_->poll(requests);
    // Keep only the newest request: an in-flight switch should not queue up.
    for (SwitchRequest& request : requests) pending_switch_ = std::move(request);
}

bool WallpaperManager::takePendingSwitch(SwitchRequest& out) {
    if (!pending_switch_) return false;
    out = std::move(*pending_switch_);
    pending_switch_.reset();
    return true;
}

bool WallpaperManager::beginPendingSwitch(EngineContext& ctx) {
    SwitchRequest request;
    if (!takePendingSwitch(request)) return false;

    TransitionConfig config = transition_config_;
    config.selection = request.transition;
    config.duration_ms = request.transition_time_ms > 0 ? request.transition_time_ms : 1000;
    if (config.selection == lwe::transition::kSelectionRandom) {
        random_seed_ += (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
        random_seed_ = random_seed_ * 1664525u + 1013904223u;
        config.selection = lwe::transition::pickRandomEffect(random_seed_);
    }
    config.continue_previous = request.continue_previous;
    if (config.continue_previous) {
        LOG_TAG_W("WALLPAPER_MGR", "transition-mode 'continue' is not implemented yet (P2); using freeze");
        config.continue_previous = false;
    }

    AudioEngine& audio = AudioEngine::instance();
    // A new switch supersedes an in-flight fade: drop its outgoing instance/group.
    if (outgoing_instance_) {
        destroyInstance(ctx, outgoing_instance_);
        audio.destroyGroup(fading_group_);
        fading_group_ = AudioEngine::kDefaultGroup;
        audio_crossfade_ = false;
    }

    const bool has_old = active_instance_ && ctx.audio_group != AudioEngine::kDefaultGroup;
    const AudioEngine::GroupId old_group = ctx.audio_group;
    const lwe::transition::AudioSwitchPlan plan =
        lwe::transition::planAudioSwitch(active_instance_ != nullptr, has_old, config.selection);

    // Capture the outgoing frame before the switch replaces the active instance.
    bool captured = false;
    if (config.selection != lwe::transition::kSelectionNone) {
        if (auto* scene = dynamic_cast<Scene2DWallpaper*>(getActiveWallpaper())) {
            if (Scene2DRuntime* runtime = scene->getRuntime()) {
                runtime->setForceOffscreen(true);
                runtime->draw();
                captured = transition_.begin(ctx, runtime->composedView(), runtime->composedImage(), surface::width(),
                                             surface::height(), config);
            }
        }
    }
    // No new transition (hard cut, or nothing to capture): drop any in-flight
    // or held snapshot so it cannot stick on top of the new wallpaper.
    if (!captured) transition_.cancel();

    if (!switchWallpaper(*this, ctx, request.path, request.is_pkg, request.properties)) {
        LOG_TAG_E("WALLPAPER_MGR", "Switch to %s failed; holding the previous frame", request.path.c_str());
        if (captured) transition_.hold();
        return true;
    }

    // switchWallpaper -> load moved the old active to outgoing_instance_ and made
    // the new instance active (ctx.audio_group is now the new group).
    const AudioEngine::GroupId new_group = ctx.audio_group;
    LOG_TAG_I("WALLPAPER_MGR", "audio switch: old_group=%u new_group=%u fade=%d cut=%d", (unsigned)old_group,
              (unsigned)new_group, plan.fade_old ? 1 : 0, plan.destroy_old_now ? 1 : 0);
    if (plan.destroy_old_now) {
        audio.destroyGroup(old_group);
        destroyInstance(ctx, outgoing_instance_);
    } else if (plan.fade_old) {
        fading_group_ = old_group;
        active_group_ = new_group;
        audio_crossfade_ = true;
    } else {
        fading_group_ = AudioEngine::kDefaultGroup;
        active_group_ = new_group;
        audio_crossfade_ = false;
    }
    LOG_TAG_I("WALLPAPER_MGR", "Switched to %s", request.path.c_str());
    return true;
}

void WallpaperManager::updateTransition(float dt) {
    transition_.update(dt);

    if (!audio_crossfade_) return;
    AudioEngine& audio = AudioEngine::instance();
    if (transition_.active()) {
        const float progress = transition_.progress();
        audio.setGroupVolume(fading_group_, 1.0f - progress);
        audio.setGroupVolume(active_group_, progress);
        return;
    }
    // Fade finished: the outgoing instance is torn down in update(ctx).
    audio.setGroupVolume(active_group_, 1.0f);
    audio_crossfade_ = false;
}
