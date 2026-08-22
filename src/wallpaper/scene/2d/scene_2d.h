#ifndef SCENE_2D_RUNTIME_H
#define SCENE_2D_RUNTIME_H

#include "core/engine_context.h"
#include "core/gfx_resource.h"

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
        GfxImage image;
        GfxView texture_view;
        GfxView attachment_view;
        int width = 0;
        int height = 0;
        sg_pixel_format pixel_format = SG_PIXELFORMAT_NONE;
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
