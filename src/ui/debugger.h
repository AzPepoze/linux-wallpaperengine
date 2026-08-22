#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

struct EngineContext;

using SandboxProjectLoader = bool (*)(const char* scene_path);

struct SandboxPreviewRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class Debugger {
   public:
    static void init();
    static void startSandbox(EngineContext& ctx, SandboxProjectLoader loader);
    static SandboxPreviewRect sandboxPreviewRect();
    static void draw(EngineContext& ctx);

   private:
    static void drawSceneTab(EngineContext& ctx);
    static void drawDiagnosticsTab(EngineContext& ctx);
    static void drawLogsTab();
    static void drawSandbox(EngineContext& ctx);
};

#endif  // DEBUG_BUILD

#endif  // DEBUGGER_H
