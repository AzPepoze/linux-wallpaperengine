#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include "../../core/gfx_resource.h"
#include "../layer.h"

class EngineContext;

class ImageLayer : public Layer {
   public:
    GfxImage img;
    GfxView cached_view;

    ImageLayer(const char* name, GfxImage img);
    virtual ~ImageLayer();

    static ImageLayer* createFromJSON(cJSON* node, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;

   private:
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
};

#endif  // IMAGE_LAYER_H
