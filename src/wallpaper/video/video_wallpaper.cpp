#include "wallpaper/video/video_wallpaper.h"

#include "shared/core/logger.h"
#include "wallpaper/2d/scene_builder.h"

bool VideoWallpaper::load(const std::string& path, EngineContext& ctx) {
    clear();

    ParsedScene parsed = SceneBuilder::buildVideoScene(path.c_str(), ctx);
    if (!applyParsedScene(std::move(parsed), ctx)) {
        LOG_TAG_E("VIDEO_WALLPAPER", "Failed to build video scene for: %s", path.c_str());
        return false;
    }
    return true;
}
