#ifndef WALLPAPER_LOADER_H
#define WALLPAPER_LOADER_H

#include <memory>

#include "shared/core/engine_context.h"
#include "wallpaper/project_info.h"
#include "wallpaper/wallpaper.h"

class WallpaperLoader {
   public:
    // Logs why a project cannot be loaded and returns false in that case.
    static bool canLoad(const ProjectInfo& info);

    // Initializes the asset context for the project and builds its wallpaper.
    // Returns nullptr (after logging) when the wallpaper fails to load.
    static std::unique_ptr<Wallpaper> load(const ProjectInfo& info, EngineContext& ctx);
};

#endif  // WALLPAPER_LOADER_H
