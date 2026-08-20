#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include <map>
#include <string>

#include "core/gfx_resource.h"
#include "wallpaper/scene/2d/layers/layer.h"

class EngineContext;

class ImageLayer : public Layer {
   public:
    GfxImage img;
    GfxView cached_view;

    ImageLayer(const char* name, GfxImage img);
    virtual ~ImageLayer();

    static ImageLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;

   private:
    struct EffectTarget {
        GfxImage image;
        GfxView texture_view;
        GfxView attachment_view;
        int width = 0;
        int height = 0;
    };
    void loadMaterial(const char* mat_rel_path, EngineContext& ctx);
    void loadModel(const char* mdl_rel_path, EngineContext& ctx);
    void updateCachedView();
    bool ensureEffectTargets();
    void renderEffectChain(EngineContext& ctx);

    GfxImage effect_images[2];
    GfxView effect_texture_views[2];
    GfxView effect_attachment_views[2];
    int effect_target_width = 0;
    int effect_target_height = 0;
    int effect_output_index = -1;
    std::map<std::string, EffectTarget> named_effect_targets;
};

#endif  // IMAGE_LAYER_H
