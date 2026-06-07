#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char engine_path[512];
    char wallpaper_path[512];
} asset_manager_t;

void asset_manager_init(asset_manager_t* am, const char* engine_path, const char* wallpaper_path);
bool asset_resolve_path(asset_manager_t* am, const char* rel_path, char* out_abs_path, int max_len);

#ifdef __cplusplus
}
#endif

#endif  // ASSET_MANAGER_H
