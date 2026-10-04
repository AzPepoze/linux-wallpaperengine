#include "wallpaper/wallpaper_manager.h"

#include <chrono>
#include <utility>

#include "app/control/control_server.h"
#include "app/wallpaper_switch.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/transition/transition_audio.h"
#include "wallpaper/wallpaper_loader.h"

bool WallpaperManager::load(const std::string& scene_directory, EngineContext& ctx) {
    if (scene_directory.empty()) return false;

    const ProjectInfo info = ProjectInfo::detect(scene_directory);
    if (!WallpaperLoader::canLoad(info)) return false;

    // The initial wallpaper gets its own audio group so the first switch can
    // crossfade. A switch pre-sets ctx.audio_group before calling load(), so
    // this only fires for the first, directly-loaded wallpaper.
    if (ctx.audio_group == AudioEngine::kDefaultGroup) {
        ctx.audio_group = AudioEngine::instance().createGroup();
        ctx.asset_mgr.setAudioGroup(ctx.audio_group);
    }

    clear();
    auto wallpaper = WallpaperLoader::load(info, ctx);
    if (!wallpaper) return false;
    active_wallpaper_ = std::move(wallpaper);
    return true;
}

void WallpaperManager::update(float dt, EngineContext& ctx) {
    if (active_wallpaper_) {
        active_wallpaper_->update(dt, ctx);
    }
}

void WallpaperManager::render(EngineContext& ctx) {
    if (active_wallpaper_) {
        active_wallpaper_->render(ctx);
    }
}

void WallpaperManager::onResize(float width, float height) {
    if (active_wallpaper_) {
        active_wallpaper_->onResize(width, height);
    }
}

void WallpaperManager::handleInput(const sapp_event* event, EngineContext& ctx) {
    if (active_wallpaper_) {
        active_wallpaper_->handleInput(event, ctx);
    }
}

void WallpaperManager::pause() {
    if (active_wallpaper_) active_wallpaper_->pause();
}

void WallpaperManager::resume() {
    if (active_wallpaper_) active_wallpaper_->resume();
}

void WallpaperManager::clear() {
    if (active_wallpaper_) {
        LOG_TAG_I("WALLPAPER_MGR", "Clearing active wallpaper instance...");
        active_wallpaper_->clear();
        active_wallpaper_.reset();
        LOG_TAG_I("WALLPAPER_MGR", "Active wallpaper cleared.");
    }
    // Drop any crossfade bookkeeping so a torn-down instance cannot keep a
    // dangling group. active_group_ is left as ctx.audio_group.
    AudioEngine::instance().destroyGroup(fading_group_);
    fading_group_ = AudioEngine::kDefaultGroup;
    audio_crossfade_ = false;
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
    // A new switch during an in-flight crossfade supersedes the older outgoing
    // group; destroy it so it cannot leak (its visual is already replaced too).
    if (audio_crossfade_) {
        audio.destroyGroup(fading_group_);
        fading_group_ = AudioEngine::kDefaultGroup;
        audio_crossfade_ = false;
    }
    const bool has_old = active_wallpaper_ && ctx.audio_group != AudioEngine::kDefaultGroup;
    const AudioEngine::GroupId old_group = ctx.audio_group;
    const lwe::transition::AudioSwitchPlan plan =
        lwe::transition::planAudioSwitch(active_wallpaper_ != nullptr, has_old, config.selection);
    if (plan.fade_old) audio.beginGroupFade(old_group);

    const AudioEngine::GroupId new_group = audio.createGroup();
    audio.setGroupVolume(new_group, plan.new_starts_silent ? 0.0f : 1.0f);
    ctx.audio_group = new_group;
    ctx.asset_mgr.setAudioGroup(new_group);

    // Capture the outgoing frame before unloading it. The switch itself always
    // happens; only the transition is skipped for `none`.
    bool captured = false;
    if (config.selection != lwe::transition::kSelectionNone) {
        if (auto* scene = dynamic_cast<Scene2DWallpaper*>(active_wallpaper_.get())) {
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
        audio.destroyGroup(new_group);
        ctx.audio_group = old_group;
        ctx.asset_mgr.setAudioGroup(old_group);
        if (lwe::transition::destroyOldGroupAfterFailure(active_wallpaper_ != nullptr)) {
            // The outgoing wallpaper was already cleared, so no owner remains for
            // its detached voices; free the group now instead of leaking it.
            audio.destroyGroup(old_group);
            ctx.audio_group = AudioEngine::kDefaultGroup;
            ctx.asset_mgr.setAudioGroup(AudioEngine::kDefaultGroup);
        } else if (plan.fade_old) {
            audio.setGroupVolume(old_group, 1.0f);
            audio.cancelGroupFade(old_group);
        }
        if (captured) transition_.hold();
        return true;
    }
    fading_group_ = plan.fade_old ? old_group : AudioEngine::kDefaultGroup;
    active_group_ = new_group;
    audio_crossfade_ = plan.fade_old;
    if (plan.destroy_old_now) audio.destroyGroup(old_group);
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
    // Transition finished: kill the outgoing group and leave the new one at full.
    audio.destroyGroup(fading_group_);
    audio.setGroupVolume(active_group_, 1.0f);
    fading_group_ = AudioEngine::kDefaultGroup;
    audio_crossfade_ = false;
}
