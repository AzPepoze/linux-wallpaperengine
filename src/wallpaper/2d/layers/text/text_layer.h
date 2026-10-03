#ifndef TEXT_LAYER_H
#define TEXT_LAYER_H

#include <string>
#include <vector>

#include "text_parser.h"
#include "wallpaper/2d/layers/image/image_layer.h"

// Rasterises a text object into an RGBA8 texture and reuses ImageLayer's draw,
// transform and effect handling.
class TextLayer : public ImageLayer {
   public:
    explicit TextLayer(const char* name);

    static TextLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;

   private:
    bool rebuild(EngineContext& ctx);
    bool rasterize(std::vector<uint32_t>& pixels, int& width, int& height, float& pixel_scale) const;
    bool resolveFontPath(EngineContext& ctx);

    TextObjectConfig config_;
    std::string current_text_;
    std::string font_path_;
};

#endif  // TEXT_LAYER_H
