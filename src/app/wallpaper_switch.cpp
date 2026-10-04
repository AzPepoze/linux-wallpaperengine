#include "app/wallpaper_switch.h"

#include <string.h>

#include "app/package_extractor.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "wallpaper/project_info.h"
#include "wallpaper/wallpaper_manager.h"

bool switchWallpaper(WallpaperManager& mgr, EngineContext& ctx, const std::string& path, bool is_pkg,
                     const std::vector<std::pair<std::string, std::string>>& properties) {
    if (path.empty()) return false;

    ctx.cli_properties = properties;
    strncpy(ctx.wallpaper_path, path.c_str(), sizeof(ctx.wallpaper_path) - 1);
    ctx.wallpaper_path[sizeof(ctx.wallpaper_path) - 1] = '\0';
    ctx.is_pkg = is_pkg;

    if (vfs::mounted()) vfs::unmount();

    if (isVideoFile(path.c_str())) {
        ctx.asset_mgr.init(ctx.engine_path, path.c_str());
        return mgr.load(path, ctx);
    }

    ctx.asset_mgr.init(ctx.engine_path, path.c_str());
    strncpy(ctx.asset_root, "extracted", sizeof(ctx.asset_root) - 1);
    WallpaperSource source;
    source.path = path;
    source.is_pkg = is_pkg;
    const std::string asset_root = prepareAssetRoot(source);
    strncpy(ctx.asset_root, asset_root.c_str(), sizeof(ctx.asset_root) - 1);
    return mgr.load(ctx.asset_root, ctx);
}
