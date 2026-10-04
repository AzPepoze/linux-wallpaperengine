#ifndef WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H
#define WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H

#include "scene_document.h"

class UserProperties;

namespace wallpaper_engine {
// Parse Wallpaper Engine scene.json into a renderer-independent document; this layer
// must not create runtime layers or GPU resources. With `user_properties`, every { "user": ..., "value": ... }
// binding (including those inside `scriptproperties`) takes the property's effective value instead of its default.
bool parseSceneFile(const char* scene_json_path, SceneDocument& out, const UserProperties* user_properties = nullptr);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_FORMAT_SCENE_PARSER_H
