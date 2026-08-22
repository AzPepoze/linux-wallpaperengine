#ifndef WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H
#define WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H

#include "scene_document.h"

namespace wallpaper_engine {

// Parse Wallpaper Engine scene.json into a renderer-independent document.
// This layer may understand Wallpaper Engine JSON, but must not create runtime
// layers or GPU resources.
bool parseSceneFile(const char* scene_json_path, SceneDocument& out);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H
