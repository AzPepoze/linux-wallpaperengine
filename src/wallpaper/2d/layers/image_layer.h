#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include <map>
#include <string>

#include "shared/graphics/gfx_resource.h"
#include "wallpaper/2d/layers/layer.h"

class EngineContext;

class ImageLayer : public Layer {
   public:
    GfxImage img;
    GfxView cached_view;
    bool solid_layer = false;
    bool is_fullscreen = false;
    bool copy_background = false;
    int color_blend_mode = 0;

    ImageLayer(const char* name, GfxImage img);
    virtual ~ImageLayer();

    static ImageLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;
    bool requiresSceneColor() const;
    void drawComposite(EngineContext& ctx, sg_view scene_view);

    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;

    wallpaper_engine::VideoTexture* bound_video_decoder = nullptr;

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
    bool ensureEffectTargets(sg_image source_image = {SG_INVALID_ID});

   public:
    void renderEffectChain(EngineContext& ctx, sg_image src_img = {SG_INVALID_ID}, sg_view src_view = {SG_INVALID_ID});

   private:
    GfxImage effect_images[2];
    GfxView effect_texture_views[2];
    GfxView effect_attachment_views[2];
    int effect_target_width = 0;
    int effect_target_height = 0;
    sg_image effect_output_image = {SG_INVALID_ID};
    sg_view effect_output_view = {SG_INVALID_ID};
    std::map<std::string, EffectTarget> named_effect_targets;
    wallpaper_engine::ImageObjectDocument alpha_document;
};

#endif  // IMAGE_LAYER_H
