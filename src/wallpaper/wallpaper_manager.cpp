#include "wallpaper/wallpaper_manager.h"

#include <chrono>
#include <utility>

#include "app/control/control_server.h"
#include "app/wallpaper_switch.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/wallpaper_loader.h"

bool WallpaperManager::load(const std::string& scene_directory, EngineContext& ctx) {
    if (scene_directory.empty()) return false;

    const ProjectInfo info = ProjectInfo::detect(scene_directory);
    if (!WallpaperLoader::canLoad(info)) return false;

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
        if (captured) transition_.hold();
        return true;
    }
    LOG_TAG_I("WALLPAPER_MGR", "Switched to %s", request.path.c_str());
    return true;
}
