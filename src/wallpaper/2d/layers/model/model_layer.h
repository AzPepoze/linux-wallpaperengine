#ifndef MODEL_LAYER_H
#define MODEL_LAYER_H

#include <memory>
#include <vector>

#include "model_data.h"
#include "wallpaper/2d/layers/image/image_layer.h"

// A layer drawn from script-made geometry (IModelData). The mesh is flattened into the layer's picture: each shape
// is textured with its material's first texture, without depth, lighting or the material's shader.
class ModelLayer : public ImageLayer {
   public:
    ModelLayer(const char* name, std::shared_ptr<ModelData> data);

    void update(float dt, EngineContext& ctx) override;

   private:
    struct ShapeBuffers {
        GfxBuffer positions;
        GfxBuffer uvs;
        GfxBuffer indices;
        size_t position_bytes = 0;
        size_t uv_bytes = 0;
        size_t index_bytes = 0;
        size_t index_count = 0;
        GfxImage texture;
        GfxView texture_view;
        std::string material;
    };

    void upload(EngineContext& ctx);

    std::shared_ptr<ModelData> data_;
    uint32_t uploaded_revision_ = 0;
    std::vector<ShapeBuffers> buffers_;
    int width_ = 1;
    int height_ = 1;
};

#endif  // MODEL_LAYER_H
