#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "core/build_config.h"

#if DEBUG_BUILD

struct EngineContext;

using SandboxProjectLoader = bool (*)(const char* scene_path);

class Debugger {
   public:
    static void init();
    static void startSandbox(EngineContext& ctx, SandboxProjectLoader loader);
    static void draw(EngineContext& ctx);

   private:
    static void drawSceneTab(EngineContext& ctx);
    static void drawDiagnosticsTab(EngineContext& ctx);
    static void drawLogsTab();
    static void drawSandbox(EngineContext& ctx);
};

#endif  // DEBUG_BUILD

#endif  // DEBUGGER_H
