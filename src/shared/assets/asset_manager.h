#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "shared/core/interfaces.h"
#include "shared/graphics/gfx_resource.h"
#include "shared/media/video_texture.h"
#include "sokol_gfx.h"

class Layer;

class AssetManager : public IAssetResolver {
   public:
    ~AssetManager();

    void init(const char* engine_path, const char* wallpaper_path);
    void updateVideoTextures(float elapsed_seconds, const std::vector<Layer*>& active_layers = {});
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

    const ActiveVideoTexture* findVideoTexture(sg_image img) const;
    const ActiveVideoTexture* findVideoTexture(const std::string& path) const;

   private:
    std::string engine_path;
    std::string wallpaper_path;
    mutable std::vector<ActiveVideoTexture> video_textures;
};

#endif  // ASSET_MANAGER_H
