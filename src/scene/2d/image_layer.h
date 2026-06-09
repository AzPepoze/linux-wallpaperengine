#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include "../layer.h"
#include "../../core/gfx_resource.h"

class EngineContext;

class ImageLayer : public Layer {
   public:
    GfxImage img;
    GfxView cached_view;

    ImageLayer(const char* name, sg_image img);
    virtual ~ImageLayer();

    static ImageLayer* createFromJSON(cJSON* node, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;

   private:
    void loadMaterial(const char* mat_rel_path, EngineContext& ctx);
    void loadModel(const char* mdl_rel_path, EngineContext& ctx);
    void updateCachedView();
};

#endif  // IMAGE_LAYER_H
