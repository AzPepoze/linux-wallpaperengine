#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>
#include <vector>

namespace Config {

    constexpr float kParallaxSmoothing = 0.1f;
    constexpr float kParallaxScale = 50.0f;
    constexpr float kDepthOffsetBase = 0.04f;
    
    constexpr float kDefaultSceneWidth = 1920.0f;
    constexpr float kDefaultSceneHeight = 1080.0f;

    const std::vector<std::string> kEngineSearchPaths = {
        "~/.local/share/Steam/steamapps/common/wallpaper_engine",
        "~/.steam/steam/steamapps/common/wallpaper_engine",
        "~/.steam/root/steamapps/common/wallpaper_engine",
        "~/Steam/steamapps/common/wallpaper_engine"
    };

    const std::map<std::string, std::string> kUniformNameMap = {
        {"scale", "g_Scale"},
        {"sens", "g_Sensitivity"},
        {"center", "g_Center"},
        {"speed", "g_Speed"},
        {"strength", "g_Strength"},
        {"direction", "g_Direction"},
        {"perspective", "g_Perspective"}
    };

} // namespace Config

#endif // CONFIG_H
