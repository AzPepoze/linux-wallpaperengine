#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "../../libs/sokol/sokol_gfx.h"

class Debugger {
   public:
    static void init();
    static void draw();

    static void setPreviewTexture(sg_image img, float aspect);

    static sg_image preview_texture;
    static sg_view preview_view;
    static float preview_aspect;

   private:
    static void drawHierarchyPanel(float width, float height);
    static void drawInspectorPanel(float width, float height);
};

#endif  // DEBUGGER_H
