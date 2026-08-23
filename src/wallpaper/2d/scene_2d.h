#ifndef SCENE_2D_RUNTIME_H
#define SCENE_2D_RUNTIME_H

#include "shared/core/engine_context.h"
#include "shared/graphics/gfx_resource.h"

class Scene2DRuntime {
   public:
    explicit Scene2DRuntime(EngineContext& ctx) : ctx(ctx) {}

    void init();
    void update(float dt);
    void draw();
    void drawParticleDiagnostics();
    void present();
    bool requiresOffscreenComposition() const;
    void updateViewport();
    void setOutputViewport(int x, int y, int width, int height);
    void resetOutputViewport();
    void clearScene();
    void cleanup();

   private:
    struct SceneTarget {
        GfxView attachment_view;
        GfxView texture_view;
        GfxImage image;
        int width = 0;
        int height = 0;
        sg_pixel_format pixel_format = SG_PIXELFORMAT_NONE;

        void reset() {
            if (attachment_view.id != SG_INVALID_ID) sg_destroy_view(attachment_view);
            if (texture_view.id != SG_INVALID_ID) sg_destroy_view(texture_view);
            if (image.id != SG_INVALID_ID) sg_destroy_image(image);
            attachment_view = {};
            texture_view = {};
            image = {};
            width = 0;
            height = 0;
            pixel_format = SG_PIXELFORMAT_NONE;
        }

        bool create(int w, int h, sg_pixel_format fmt) {
            reset();
            sg_image_desc image_desc = {};
            image_desc.usage.color_attachment = true;
            image_desc.width = w;
            image_desc.height = h;
            image_desc.pixel_format = fmt;
            image = sg_make_image(&image_desc);
            if (image.id == SG_INVALID_ID && fmt != SG_PIXELFORMAT_RGBA8) {
                image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
                image = sg_make_image(&image_desc);
            }
            if (image.id == SG_INVALID_ID) return false;

            sg_view_desc texture_desc = {};
            texture_desc.texture.image = image;
            texture_view = sg_make_view(&texture_desc);

            sg_view_desc attachment_desc = {};
            attachment_desc.color_attachment.image = image;
            attachment_view = sg_make_view(&attachment_desc);

            if (texture_view.id == SG_INVALID_ID || attachment_view.id == SG_INVALID_ID) {
                reset();
                return false;
            }

            width = w;
            height = h;
            pixel_format = image_desc.pixel_format;
            return true;
        }
    };

    EngineContext& ctx;
    int output_x = 0;
    int output_y = 0;
    int output_width = 0;
    int output_height = 0;
    SceneTarget scene_targets[2];
    SceneTarget bloom_targets[2];
    int scene_output_index = -1;
    class ShaderPass* bloom_pass_extract = nullptr;
    class ShaderPass* bloom_pass_blur_v = nullptr;
    class ShaderPass* bloom_pass_blur_h = nullptr;
    class ShaderPass* bloom_pass_combine = nullptr;

    void initBloomPipelines();
    void destroyBloomPipelines();
    sg_pixel_format compositionPixelFormat() const;
    bool ensureSceneTargets(int width, int height);
    bool ensureBloomTargets(int width, int height);
    int renderBloom(int current_target_index, int width, int height);
    void drawDirect();
    void drawOffscreen();
};

#endif  // SCENE_2D_RUNTIME_H
