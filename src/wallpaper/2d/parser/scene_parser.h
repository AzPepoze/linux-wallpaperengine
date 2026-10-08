#ifndef WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H
#define WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H

#include "scene_document.h"

class UserProperties;

namespace wallpaper_engine {
// Parses scene.json without creating layers or GPU resources; user_properties override defaults.
bool parseSceneFile(const char* scene_json_path, SceneDocument& out, const UserProperties* user_properties = nullptr);

// Parses one scene object; false when the JSON is invalid or has no id.
bool parseSceneObject(const char* object_json, SceneObjectDocument& out);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H
