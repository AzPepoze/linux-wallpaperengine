#ifndef SCENE_PARSER_INTERNAL_H
#define SCENE_PARSER_INTERNAL_H

#include <string>
#include <vector>

#include "scene_document.h"

struct cJSON;

namespace wallpaper_engine {
namespace scene_parser_detail {

// Value readers: each unwraps a `{ "value": ... }` property object and returns false when absent or unparsable.
bool parseVec(const cJSON* raw, float* out, int count);
bool parseFloat(const cJSON* raw, float& out);
bool parseBool(const cJSON* raw, bool fallback = false);
bool parseString(const cJSON* raw, std::string& out);

// Reads `script` and `scriptproperties` from a property object; leaves `out` empty for plain values.
void readScript(const cJSON* property, ScriptedValue& out);

// Direct member lookup / plain string read, with no property-object unwrapping.
const cJSON* member(const cJSON* object, const char* key);
void readPlainString(const cJSON* object, const char* key, std::string& out);

// A scene object and its nested groups.
SceneObjectDocument parseObject(const cJSON* object);
void parseEffects(const cJSON* object, std::vector<EffectInstanceDocument>& out);
void parseAnimationLayers(const cJSON* array, std::vector<AnimationLayerDocument>& out);
void parsePropertyAnimation(const cJSON* object, const char* property, std::vector<PropertyAnimationDocument>& out);
void parseCameraPath(const cJSON* object, SceneCameraDocument& out);

}  // namespace scene_parser_detail
}  // namespace wallpaper_engine

#endif  // SCENE_PARSER_INTERNAL_H
