#ifndef MODEL_MATERIAL_H
#define MODEL_MATERIAL_H

#include <cjson/cJSON.h>

#include <map>
#include <string>
#include <vector>

#include "shared/graphics/shader/shader_compiler.h"
#include "wallpaper/user_properties.h"

// Render state of a model material pass from `blending`, `cullmode`, `depthtest` and `depthwrite`.
MeshPipelineState parseMeshPipelineState(const cJSON* pass);

// Material constant values taken from user properties through the pass's `usershadervalues`.
std::map<std::string, std::vector<float>> resolveUserShaderValues(const cJSON* pass, const UserProperties& properties);

// Shaders test combos in upper case (`#ifdef METAL`), while model materials may author them in lower case.
std::map<std::string, int> upperCaseCombos(const std::map<std::string, int>& combos);

#endif  // MODEL_MATERIAL_H
