#ifndef FRAME_LOOP_H
#define FRAME_LOOP_H

#include "shared/core/engine_context.h"
#include "sokol_app.h"
#include "wallpaper/wallpaper_manager.h"

void runFrame(EngineContext& ctx, WallpaperManager& mgr);
void handleAppEvent(const sapp_event* e, EngineContext& ctx, WallpaperManager& mgr);

#endif  // FRAME_LOOP_H
