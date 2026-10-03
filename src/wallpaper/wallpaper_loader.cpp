#include "wallpaper/wallpaper_loader.h"

#include <cstring>

#include "shared/core/build_config.h"
#include "shared/core/logger.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/video/video_wallpaper.h"
#include "wallpaper/web/web_wallpaper.h"

namespace {
std::unique_ptr<Wallpaper> createWallpaper(ProjectType type, EngineContext& ctx) {
    switch (type) {
        case ProjectType::Scene:
            return std::make_unique<Scene2DWallpaper>(ctx);
        case ProjectType::Video:
            return std::make_unique<VideoWallpaper>(ctx);
#if LWE_WEB
        case ProjectType::Web:
            return std::make_unique<WebWallpaper>(ctx);
#endif
        default:
            return nullptr;
    }
}
}  // namespace

bool WallpaperLoader::canLoad(const ProjectInfo& info) {
    switch (info.type) {
        case ProjectType::Scene:
        case ProjectType::Video:
            return true;
        case ProjectType::Web:
            if (!LWE_WEB) {
                LOG_TAG_E("WALLPAPER_MGR", "built without web support (xmake f --web=y)");
                return false;
            }
            return true;
        case ProjectType::Unsupported:
            LOG_TAG_E("WALLPAPER_MGR", "Unsupported wallpaper type: %s", info.type_name.c_str());
            return false;
        case ProjectType::None:
            break;
    }
    LOG_TAG_W("WALLPAPER_MGR", "No supported wallpaper found in directory: %s", info.root.c_str());
    return false;
}

std::unique_ptr<Wallpaper> WallpaperLoader::load(const ProjectInfo& info, EngineContext& ctx) {
    strncpy(ctx.asset_root, info.root.c_str(), sizeof(ctx.asset_root) - 1);
    ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
    ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);

    auto wallpaper = createWallpaper(info.type, ctx);
    if (!wallpaper) return nullptr;
    if (!wallpaper->load(info.entry, ctx)) {
        if (info.type == ProjectType::Scene)
            LOG_TAG_W("WALLPAPER_MGR", "Could not parse 2D scene: %s", info.entry.c_str());
        return nullptr;
    }
    return wallpaper;
}
