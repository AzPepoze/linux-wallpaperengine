#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "../../libs/sokol/sokol_gfx.h"
#include "../core/gfx_resource.h"

struct EngineContext;

class Debugger {
   public:
    static void init();
    static void draw(EngineContext& ctx);

    static void setPreviewTexture(sg_image img, float aspect);

    static GfxImage preview_texture;
    static GfxView preview_view;
    static float preview_aspect;

   private:
    static void drawHierarchyPanel(EngineContext& ctx, float width, float height);
    static void drawInspectorPanel(EngineContext& ctx, float width, float height);
};

#endif  // DEBUGGER_H
