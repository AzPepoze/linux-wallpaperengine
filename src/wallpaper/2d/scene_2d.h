#ifndef SCENE_2D_RUNTIME_H
#define SCENE_2D_RUNTIME_H

#include "shared/core/engine_context.h"
#include "shared/graphics/diagnostics/gpu_trace.h"
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
        uint64_t generation = 0;

        void reset(const char* reason = "reset", const char* name = "scene_target") {
            if (image.id != SG_INVALID_ID) {
                gpu_trace_rt_destroy(reason, name, image.id, texture_view.id, attachment_view.id, width, height,
                                     generation);
            }
            attachment_view = {};
            texture_view = {};
            image = {};
            width = 0;
            height = 0;
            pixel_format = SG_PIXELFORMAT_NONE;
        }

        bool create(int w, int h, sg_pixel_format fmt, const char* kind = "scene", const char* name = "scene_target") {
            reset("recreate", name);
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
                reset("error_rollback", name);
                return false;
            }

            width = w;
            height = h;
            pixel_format = image_desc.pixel_format;
            generation = gpu_trace_next_target_generation();
            gpu_trace_rt_create(kind, name, image.id, texture_view.id, attachment_view.id, width, height, generation);
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

    GfxPipeline pip_bloom_extract;
    GfxPipeline pip_bloom_blur_h;
    GfxPipeline pip_bloom_blur_v;

    void initBloomPipelines();
    sg_pixel_format compositionPixelFormat() const;
    bool ensureSceneTargets(int width, int height);
    bool ensureBloomTargets(int width, int height);
    void renderBloom(int current_target_index, int width, int height);
    void drawDirect();
    void drawOffscreen();
};

#endif  // SCENE_2D_RUNTIME_H
