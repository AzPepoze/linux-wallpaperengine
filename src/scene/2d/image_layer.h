#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include "../layer.h"

class ImageLayer : public Layer {
   public:
    sg_image img;

    ImageLayer(const char* name, sg_image img);
    virtual ~ImageLayer();

    void update(float dt) override;
    void draw() override;
    void showInspector() override;
};

#endif  // IMAGE_LAYER_H
