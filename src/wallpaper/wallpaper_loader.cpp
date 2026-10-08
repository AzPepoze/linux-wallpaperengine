#include "wallpaper/wallpaper_loader.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "shared/core/build_config.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/video/video_wallpaper.h"
#include "wallpaper/web/web_wallpaper.h"

namespace {
// Effective user properties: project defaults, then the desktop GUI's saved values, then --set-property.

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

void applyVideoProperties(const VideoProperties& video, EngineContext& ctx) {
    ctx.asset_mgr->setVideoPlayback(video.rate, video.volume);
    // An explicit --cover on the command line keeps priority over the project's fit.
    if (video.fit != VideoFit::Default && ctx.scene.scaling_mode == SCALING_FIT)
        ctx.scene.scaling_mode = video.fit == VideoFit::Fill ? SCALING_COVER : SCALING_FIT;
}
}  // namespace

UserProperties WallpaperLoader::prepareProperties(const ProjectInfo& info,
                                                  const std::vector<std::pair<std::string, std::string>>& overrides) {
    const std::string root = info.root == vfs::kRoot && vfs::mounted() ? vfs::sourceDirectory() : info.root;
    UserProperties properties;
    properties.loadProject(root + "/project.json");
    if (const char* home = getenv("HOME")) {
        std::ifstream file(std::string(home) + "/.config/linux-wallpaperengine-gui/config.json");
        std::stringstream text;
        text << file.rdbuf();
        properties.applySaved(text.str(), std::filesystem::path(root).filename().string());
    }
    for (const auto& [key, value] : overrides) properties.setFromString(key, value);
    return properties;
}

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
    ctx.asset_mgr->initWallpaper(ctx.asset_root);
    ctx.user_properties = prepareProperties(info, ctx.cli_properties);
    ScriptEngine::instance().setAssetsDir(std::string(ctx.engine_path) + "/assets");
    ScriptEngine::instance().setWallpaperId(std::filesystem::path(info.root).filename().string());
    ctx.asset_mgr->prefetchPackageTextures();
    struct ReleaseDecoded {
        AssetManager& assets;
        ~ReleaseDecoded() {
            assets.releaseDecodedTextures();
        }
    } release_decoded{*ctx.asset_mgr};

    if (info.type == ProjectType::Video) applyVideoProperties(info.video, ctx);
    auto wallpaper = createWallpaper(info.type, ctx);
    if (!wallpaper) return nullptr;
    if (!wallpaper->load(info.entry, ctx)) {
        if (info.type == ProjectType::Scene)
            LOG_TAG_W("WALLPAPER_MGR", "Could not parse 2D scene: %s", info.entry.c_str());
        return nullptr;
    }
    return wallpaper;
}
