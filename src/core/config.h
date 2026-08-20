#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>
#include <vector>

namespace Config {

// Legacy ParticleSystem internals still reference this constant, but normal
// camera parallax is now resolved at the Layer level and feeds particles a
// translated layer origin, so this scale is no longer part of active parallax behavior.
constexpr float kParallaxScale = 50.0f;
constexpr float kDepthOffsetBase = 0.04f;

constexpr float kDefaultSceneWidth = 1920.0f;
constexpr float kDefaultSceneHeight = 1080.0f;

const std::vector<std::string> kEngineSearchPaths = {
    "~/.local/share/Steam/steamapps/common/wallpaper_engine", "~/.steam/steam/steamapps/common/wallpaper_engine",
    "~/.steam/root/steamapps/common/wallpaper_engine", "~/Steam/steamapps/common/wallpaper_engine"};

const std::map<std::string, std::string> kUniformNameMap = {
    {"scale", "g_Scale"},       {"sens", "g_Sensitivity"},    {"center", "g_Center"},          {"speed", "g_Speed"},
    {"strength", "g_Strength"}, {"direction", "g_Direction"}, {"perspective", "g_Perspective"}};

}  // namespace Config

#endif  // CONFIG_H
