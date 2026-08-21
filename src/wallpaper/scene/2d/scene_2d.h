#ifndef SCENE_2D_RUNTIME_H
#define SCENE_2D_RUNTIME_H

#include "core/engine_context.h"

class Scene2DRuntime {
   public:
    explicit Scene2DRuntime(EngineContext& ctx) : ctx(ctx) {}

    void init();
    void update(float dt);
    void draw();
    void updateViewport();
    void setOutputViewport(int x, int y, int width, int height);
    void resetOutputViewport();
    void clearScene();
    void cleanup();

   private:
    EngineContext& ctx;
    int output_x = 0;
    int output_y = 0;
    int output_width = 0;
    int output_height = 0;
};

#endif  // SCENE_2D_RUNTIME_H
