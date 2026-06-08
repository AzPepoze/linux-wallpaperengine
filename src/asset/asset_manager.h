#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <string>

#include "../../libs/sokol/sokol_gfx.h"

class AssetManager {
   public:
    void init(const char* engine_path, const char* wallpaper_path);
    bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const;

    // High-level resolvers
    sg_image resolveTexture(const char* name, std::string* out_path = nullptr, int image_index = 0) const;
    sg_image resolveMaterialTexture(const char* mat_rel_path, std::string* out_path = nullptr) const;

   private:
    std::string engine_path;
    std::string wallpaper_path;
};

#endif  // ASSET_MANAGER_H
