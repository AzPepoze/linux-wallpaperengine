#include "asset_manager.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void asset_manager_init(asset_manager_t* am, const char* engine_path, const char* wallpaper_path) {
    strncpy(am->engine_path, engine_path, sizeof(am->engine_path) - 1);
    strncpy(am->wallpaper_path, wallpaper_path, sizeof(am->wallpaper_path) - 1);
}

bool asset_resolve_path(asset_manager_t* am, const char* rel_path, char* out_abs_path, int max_len) {
    // 1. Try Local Wallpaper Path (or tmp extracted)
    snprintf(out_abs_path, max_len, "%s/%s", am->wallpaper_path, rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 2. Try Engine Assets Path
    snprintf(out_abs_path, max_len, "%s/assets/%s", am->engine_path, rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 3. Try specifically under assets/materials/ (common for particles)
    snprintf(out_abs_path, max_len, "%s/assets/materials/%s", am->engine_path, rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 4. Try specifically under assets/presets/ (common for material JSONs)
    // Note: many presets are nested, e.g. presets/ember/materials/presets/emberglow.json
    // We try to find the filename in a few common patterns
    const char* filename = strrchr(rel_path, '/');
    if (filename) {
        filename++;  // skip slash
        // Extract the "ember" part if path is like materials/presets/emberglow.json
        // This is a bit hacky but covers many standard assets
        if (strstr(rel_path, "materials/presets/") == rel_path) {
            char preset_name[64];
            const char* start = rel_path + 18;
            const char* end = strstr(start, ".json");
            if (end) {
                size_t len = end - start;
                // remove "glow" or "trail" suffixes to find the folder
                if (len > 4) len -= 4;
                strncpy(preset_name, start, len);
                preset_name[len] = '\0';

                snprintf(out_abs_path, max_len, "%s/assets/presets/%s/%s", am->engine_path, preset_name, rel_path);
                if (access(out_abs_path, F_OK) == 0) return true;
            }
        }
    }

    // 5. Fallback: Search all of assets/ for the filename if path is relative
    // (This is slow but reliable for missing built-ins)
    // For now, we just check root engine path
    snprintf(out_abs_path, max_len, "%s/%s", am->engine_path, rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    return false;
}
