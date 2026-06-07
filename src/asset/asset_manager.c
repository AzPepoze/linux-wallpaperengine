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

    // 3. Try tmp directory directly
    snprintf(out_abs_path, max_len, "tmp/%s", rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    return false;
}
