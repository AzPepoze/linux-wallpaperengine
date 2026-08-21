#ifndef PARTICLE_PARSER_H
#define PARTICLE_PARSER_H

#include <cjson/cJSON.h>

#include "linmath.h"

// Parses the scalar/vector value forms accepted by particle JSON files.
class ParticleParser {
   public:
    static void readVec3(const cJSON* node, vec3 out);
    static float readFloat(const cJSON* node);
};

#endif  // PARTICLE_PARSER_H
