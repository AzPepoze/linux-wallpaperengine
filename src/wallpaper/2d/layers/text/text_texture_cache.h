#ifndef TEXT_TEXTURE_CACHE_H
#define TEXT_TEXTURE_CACHE_H

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "shared/graphics/gfx_resource.h"
#include "wallpaper/2d/layers/text/text_raster.h"

struct TextTexture {
    GfxImage image;
    GfxView view;
    GfxBuffer quad;  // draws only the cropped part of the layer, when cropped
    bool cropped = false;
    float size[2] = {0.0f, 0.0f};
};

struct RasterOutput {
    bool ok = false;
    TextRasterResult result;
    std::vector<uint16_t> texels;
};

// Only the render thread may call it.
class TextTextureCache {
   public:
    std::shared_ptr<TextTexture> find(const std::string& key);
    void remember(const std::string& key, const std::shared_ptr<TextTexture>& texture);
    std::shared_future<RasterOutput> rasterFor(const std::string& key, const TextRasterRequest& request, bool crop);

   private:
    void purgeFinishedRasters();

    // Weak, so a texture is freed when the last layer using it is destroyed.
    std::map<std::string, std::weak_ptr<TextTexture>> textures_;
    std::map<std::string, std::shared_future<RasterOutput>> flights_;
};

#endif  // TEXT_TEXTURE_CACHE_H
