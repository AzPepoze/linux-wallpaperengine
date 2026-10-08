#ifndef TEXT_LAYER_H
#define TEXT_LAYER_H

#include <memory>
#include <string>
#include <vector>

#include "text_parser.h"
#include "text_raster.h"
#include "text_texture_cache.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/script/scene_script.h"

struct PendingRaster;

// Rasterises text into an RGBA float texture and reuses ImageLayer's draw and effects.
class TextLayer : public ImageLayer {
   public:
    explicit TextLayer(const char* name);

    // Waits for the first texture, for callers that build synchronously.
    static TextLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx,
                                         std::shared_ptr<TextTextureCache> cache = {});
    // Starts rasterizing on a worker; poll pollPreparation() until it returns true.
    static TextLayer* createPending(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx,
                                    std::shared_ptr<TextTextureCache> cache = {});
    // Uploads a finished raster on the calling (render) thread. True when nothing is pending.
    bool pollPreparation();

    void update(float dt, EngineContext& ctx) override;

    bool propertyGetString(const std::string& name, std::string& out) const;
    bool propertySetString(const std::string& name, const std::string& value);
    bool propertyGetNumber(const std::string& name, double& out) const;
    bool propertySetNumber(const std::string& name, double value);
    bool propertyGetBool(const std::string& name, bool& out) const;
    bool propertySetBool(const std::string& name, bool value);
    bool propertyGetVector(const std::string& name, double out[3]) const;
    bool propertySetVector(const std::string& name, const double value[3]);

   protected:
    ScreenRect screenRect(EngineContext& ctx) const override;

   private:
    // Starts a raster for the current settings; the layer keeps its previous texture until it is uploaded.
    bool beginPreparation(EngineContext& ctx);
    bool canCropTexture() const;
    void refreshForEffects(EngineContext& ctx);
    TextRasterRequest rasterRequest() const;
    std::shared_ptr<TextTexture> uploadTexture(const std::string& key, const RasterOutput& output);
    bool resolveFontPath(EngineContext& ctx);
    void useTexture(const std::shared_ptr<TextTexture>& texture);

    TextObjectConfig config_;
    std::string current_text_;
    std::string font_path_;
    bool needs_rebuild_ = false;
    std::shared_ptr<TextTexture> texture_;
    std::shared_ptr<PendingRaster> pending_;
    std::shared_ptr<TextTextureCache> cache_;
};

#endif  // TEXT_LAYER_H
