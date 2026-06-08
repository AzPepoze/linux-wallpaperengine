#ifndef DEBUGGER_H
#define DEBUGGER_H

class Debugger {
   public:
    static void init();
    static void draw();

   private:
    static void drawHierarchyPanel(float width, float height);
    static void drawInspectorPanel(float width, float height);
};

#endif  // DEBUGGER_H
