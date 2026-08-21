#ifndef PARTICLE_PARSER_H
#define PARTICLE_PARSER_H

#include <cjson/cJSON.h>

#include <string>

#include "formats/wallpaper_engine/scene/scene_document.h"
#include "linmath.h"

struct ParticleObjectConfig {
    std::string name;
    std::string particle_path;
};

// Parses the scalar/vector value forms accepted by particle JSON files.
class ParticleParser {
   public:
    static void readVec3(const cJSON* node, vec3 out);
    static float readFloat(const cJSON* node);
    static ParticleObjectConfig parseObject(const wallpaper_engine::SceneObjectDocument& document);
};

#endif  // PARTICLE_PARSER_H
