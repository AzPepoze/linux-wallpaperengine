#ifndef WALLPAPER_SWITCH_H
#define WALLPAPER_SWITCH_H

#include <string>
#include <utility>
#include <vector>

struct EngineContext;
class WallpaperManager;

// Loads `path` into the running manager the way a fresh launch would:
// re-initializes the asset context for directory and package wallpapers.
bool switchWallpaper(WallpaperManager& mgr, EngineContext& ctx, const std::string& path, bool is_pkg,
                     const std::vector<std::pair<std::string, std::string>>& properties);

#endif  // WALLPAPER_SWITCH_H
