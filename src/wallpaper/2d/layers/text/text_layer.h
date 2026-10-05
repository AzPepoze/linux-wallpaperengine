#ifndef TEXT_LAYER_H
#define TEXT_LAYER_H

#include <memory>
#include <string>
#include <vector>

#include "text_parser.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/script/scene_script.h"

// Rasterises a text object into a floating-point RGBA texture and reuses ImageLayer's draw,
// transform and effect handling.
class TextLayer : public ImageLayer {
   public:
    explicit TextLayer(const char* name);

    static TextLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

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
    bool rebuild(EngineContext& ctx);
    bool rasterize(std::vector<float>& pixels, int& width, int& height, float& pixel_scale) const;
    bool resolveFontPath(EngineContext& ctx);

    TextObjectConfig config_;
    std::string current_text_;
    std::string font_path_;
    bool needs_rebuild_ = false;
};

#endif  // TEXT_LAYER_H
