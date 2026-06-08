#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "../../libs/sokol/sokol_gfx.h"

class Debugger {
   public:
    static void init();
    static void draw();

    static sg_image preview_texture;
    static float preview_aspect;

   private:
    static void drawHierarchyPanel(float width, float height);
    static void drawInspectorPanel(float width, float height);
};

#endif  // DEBUGGER_H
