#ifndef SCENE_RENDERER_H
#define SCENE_RENDERER_H

#include "../core/engine_context.h"

class SceneRenderer {
   public:
    SceneRenderer(EngineContext& ctx) : ctx(ctx) {}

    void init();
    void update(float dt);
    void draw();
    void updateViewport();
    void cleanup();

   private:
    EngineContext& ctx;
};

#endif  // SCENE_RENDERER_H
