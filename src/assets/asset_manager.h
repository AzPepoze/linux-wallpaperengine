#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "../core/gfx_resource.h"
#include "../core/interfaces.h"
#include "formats/wallpaper_engine/texture/video_texture.h"
#include "sokol_gfx.h"

class AssetManager : public IAssetResolver {
   public:
    ~AssetManager();

    void init(const char* engine_path, const char* wallpaper_path);
    void updateVideoTextures(float elapsed_seconds);
    void clearVideoTextures();
    bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const override;

    // High-level resolvers
    GfxImage resolveTexture(const char* name, std::string* out_path = nullptr, int image_index = 0) const override;
    GfxImage resolveMaterialTexture(const char* mat_rel_path, std::string* out_path = nullptr) const override;

    struct ActiveVideoTexture {
        std::string path;
        sg_image image = {};
        std::unique_ptr<wallpaper_engine::VideoTexture> decoder;
        float elapsed_seconds = 0.0f;
    };

    const std::vector<ActiveVideoTexture>& getVideoTextures() const {
        return video_textures;
    }

   private:
    std::string engine_path;
    std::string wallpaper_path;
    mutable std::vector<ActiveVideoTexture> video_textures;
};

#endif  // ASSET_MANAGER_H
