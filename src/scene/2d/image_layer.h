#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include "../layer.h"

class ImageLayer : public Layer {
   public:
    sg_image img;

    ImageLayer(const char* name, sg_image img);
    virtual ~ImageLayer();

    static ImageLayer* createFromJSON(cJSON* node);

    void update(float dt) override;
    void draw() override;
    void drawDebug() override;
    void showInspector() override;

   private:
    void loadMaterial(const char* mat_rel_path);
    void loadModel(const char* mdl_rel_path);
};

#endif  // IMAGE_LAYER_H
