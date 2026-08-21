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
    };

    EngineContext& ctx;
    int output_x = 0;
    int output_y = 0;
    int output_width = 0;
    int output_height = 0;
    SceneTarget scene_targets[2];
    int scene_output_index = -1;

    bool ensureSceneTargets(int width, int height);
    void drawDirect();
    void drawOffscreen();
};

#endif  // SCENE_2D_RUNTIME_H
