#include "wallpaper/wallpaper_manager.h"

#include "shared/core/logger.h"
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
