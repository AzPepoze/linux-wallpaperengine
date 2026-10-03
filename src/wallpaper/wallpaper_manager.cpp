#include "wallpaper/wallpaper_manager.h"

#include <cjson/cJSON.h>
#include <dirent.h>
#include <strings.h>
#include <unistd.h>

#include <cstring>

#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/video/video_wallpaper.h"

namespace {
void initAssetContext(const std::string& asset_root, EngineContext& ctx) {
    strncpy(ctx.asset_root, asset_root.c_str(), sizeof(ctx.asset_root) - 1);
    ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
    ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);
}

}  // namespace

bool WallpaperManager::isVideoFile(const char* path) {
    if (!path) return false;
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".webm") == 0 || strcasecmp(ext, ".mkv") == 0 ||
            strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".wmv") == 0);
}

bool WallpaperManager::isHtmlFile(const char* path) {
    if (!path) return false;
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0);
}

bool WallpaperManager::load(const std::string& scene_directory, EngineContext& ctx) {
    if (scene_directory.empty()) return false;

    if (isVideoFile(scene_directory.c_str()) && access(scene_directory.c_str(), F_OK) == 0) {
        clear();
        initAssetContext(scene_directory, ctx);

        auto video_wp = std::make_unique<VideoWallpaper>(ctx);
        if (video_wp->load(scene_directory, ctx)) {
            active_wallpaper_ = std::move(video_wp);
            return true;
        }
        return false;
    }

    std::string scene_path = scene_directory + "/scene.json";
    if (access(scene_path.c_str(), F_OK) == 0) {
        clear();
        initAssetContext(scene_directory, ctx);

        auto scene_wp = std::make_unique<Scene2DWallpaper>(ctx);
        if (scene_wp->load(scene_path, ctx)) {
            active_wallpaper_ = std::move(scene_wp);
            return true;
        }
        LOG_TAG_W("WALLPAPER_MGR", "Could not parse 2D scene: %s", scene_path.c_str());
        return false;
    }

    std::string project_path = scene_directory + "/project.json";
    if (access(project_path.c_str(), F_OK) == 0) {
        char* json_str = read_file_to_string(project_path.c_str());
        if (json_str) {
            cJSON* root = cJSON_Parse(json_str);
            free(json_str);
            if (root) {
                cJSON* type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
                if (cJSON_IsString(type_item) && type_item->valuestring && type_item->valuestring[0] != '\0') {
                    if (strcasecmp(type_item->valuestring, "web") == 0) {
                        LOG_TAG_E("WALLPAPER_MGR", "web wallpapers are not supported yet");
                        cJSON_Delete(root);
                        return false;
                    }
                    if (strcasecmp(type_item->valuestring, "scene") != 0 &&
                        strcasecmp(type_item->valuestring, "video") != 0) {
                        LOG_TAG_E("WALLPAPER_MGR", "Unsupported wallpaper type: %s", type_item->valuestring);
                        cJSON_Delete(root);
                        return false;
                    }
                }
                cJSON* file_item = cJSON_GetObjectItemCaseSensitive(root, "file");
                if (cJSON_IsString(file_item) && file_item->valuestring && file_item->valuestring[0] != '\0') {
                    std::string target_file = scene_directory + "/" + file_item->valuestring;
                    if (access(target_file.c_str(), F_OK) == 0) {
                        cJSON_Delete(root);
                        clear();
                        initAssetContext(scene_directory, ctx);

                        if (isVideoFile(target_file.c_str())) {
                            auto video_wp = std::make_unique<VideoWallpaper>(ctx);
                            if (video_wp->load(target_file, ctx)) {
                                active_wallpaper_ = std::move(video_wp);
                                return true;
                            }
                        } else {
                            auto scene_wp = std::make_unique<Scene2DWallpaper>(ctx);
                            if (scene_wp->load(target_file, ctx)) {
                                active_wallpaper_ = std::move(scene_wp);
                                return true;
                            }
                        }
                        return false;
                    }
                }
                cJSON_Delete(root);
            }
        }
    }

    DIR* dir = opendir(scene_directory.c_str());
    if (dir) {
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if (isVideoFile(entry->d_name)) {
                std::string video_path = scene_directory + "/" + entry->d_name;
                closedir(dir);
                clear();
                initAssetContext(scene_directory, ctx);

                auto video_wp = std::make_unique<VideoWallpaper>(ctx);
                if (video_wp->load(video_path, ctx)) {
                    active_wallpaper_ = std::move(video_wp);
                    return true;
                }
                return false;
            }
        }
        closedir(dir);
    }

    LOG_TAG_W("WALLPAPER_MGR", "No supported wallpaper found in directory: %s", scene_directory.c_str());
    return false;
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

void WallpaperManager::clear() {
    if (active_wallpaper_) {
        LOG_TAG_I("WALLPAPER_MGR", "Clearing active wallpaper instance...");
        active_wallpaper_->clear();
        active_wallpaper_.reset();
        LOG_TAG_I("WALLPAPER_MGR", "Active wallpaper cleared.");
    }
}
