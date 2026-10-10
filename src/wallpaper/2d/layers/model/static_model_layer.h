#ifndef STATIC_MODEL_LAYER_H
#define STATIC_MODEL_LAYER_H

#include <memory>
#include <string>
#include <vector>

#include "linmath.h"
#include "shared/graphics/gfx_resource.h"
#include "shared/graphics/passes/shader_pass.h"
#include "static_mesh.h"
#include "wallpaper/2d/layers/layer.h"

// A scene `model` object: a static .mdl mesh drawn in 3D with depth, one material per submesh.
class StaticModelLayer : public Layer {
   public:
    explicit StaticModelLayer(const char* name) : Layer(name) {}

    // Null when the mesh cannot be read or none of its submeshes has a usable material.
    static StaticModelLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float, EngineContext&) override {}
    void draw(EngineContext& ctx) override;
    bool needsDepthBuffer() const override {
        return true;
    }

   private:
    struct Material {
        std::string path;
        std::unique_ptr<ShaderPass> pass;
        GfxPipeline pipeline;
        sg_bindings bindings = {};
        bool translucent = false;
    };

    bool loadMesh(const char* mdl_path, EngineContext& ctx);
    // Index into materials_, loading the material on first use; -1 when it cannot be drawn.
    int materialFor(const std::string& path, EngineContext& ctx);
    bool loadMaterial(const std::string& path, EngineContext& ctx, Material& out) const;
    void bindMaterial(EngineContext& ctx, Material& material) const;

    GfxBuffer vertex_buffer_;
    GfxBuffer index_buffer_;
    std::vector<Material> materials_;
    std::vector<MeshDraw> draws_;
    mat4x4 model_matrix_;
};

#endif  // STATIC_MODEL_LAYER_H
