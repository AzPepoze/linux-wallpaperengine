#include "asset_provider.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "shared/core/vfs.h"

EngineAssetProvider::EngineAssetProvider(std::string engine_path) : engine_path(std::move(engine_path)) {}

bool EngineAssetProvider::resolvePath(const char* rel_path, char* out_abs_path, int max_len) const {
    if (!rel_path || !out_abs_path || max_len <= 0 || engine_path.empty()) return false;

    snprintf(out_abs_path, max_len, "%s/assets/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    snprintf(out_abs_path, max_len, "%s/assets/materials/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    const char* filename = strrchr(rel_path, '/');
    if (filename) {
        filename++;
        if (strstr(rel_path, "materials/presets/") == rel_path) {
            char preset_name[64];
            const char* start = rel_path + 18;
            const char* end = strstr(start, ".json");
            if (end) {
                size_t len = end - start;
                if (len > 4) len -= 4;
                strncpy(preset_name, start, len);
                preset_name[len] = '\0';
                snprintf(out_abs_path, max_len, "%s/assets/presets/%s/%s", engine_path.c_str(), preset_name, rel_path);
                if (access(out_abs_path, F_OK) == 0) return true;
            }
        }
    }

    snprintf(out_abs_path, max_len, "%s/%s", engine_path.c_str(), rel_path);
    return access(out_abs_path, F_OK) == 0;
}

WallpaperAssetProvider::WallpaperAssetProvider(std::string wallpaper_path)
    : wallpaper_path(std::move(wallpaper_path)) {}

bool WallpaperAssetProvider::resolvePath(const char* rel_path, char* out_abs_path, int max_len) const {
    if (!rel_path || !out_abs_path || max_len <= 0 || wallpaper_path.empty()) return false;

    snprintf(out_abs_path, max_len, "%s/%s", wallpaper_path.c_str(), rel_path);
    if (vfs::exists(out_abs_path)) return true;

    snprintf(out_abs_path, max_len, "%s/materials/%s", wallpaper_path.c_str(), rel_path);
    return vfs::exists(out_abs_path);
}

bool InternalAssetProvider::resolvePath(const char* rel_path, char* out_abs_path, int max_len) const {
    if (!rel_path || !out_abs_path || max_len <= 0) return false;
    if (rel_path[0] == '/' && access(rel_path, F_OK) == 0) {
        snprintf(out_abs_path, max_len, "%s", rel_path);
        return true;
    }
    return false;
}
