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
    void clearScene();
    void cleanup();

   private:
    EngineContext& ctx;
};

#endif  // SCENE_2D_RUNTIME_H
