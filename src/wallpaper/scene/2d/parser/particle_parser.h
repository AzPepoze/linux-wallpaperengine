#ifndef PARTICLE_PARSER_H
#define PARTICLE_PARSER_H

#include <cjson/cJSON.h>

#include <string>
#include <vector>

#include "formats/wallpaper_engine/scene/scene_document.h"
#include "linmath.h"

struct ParticleObjectConfig {
    std::string name;
    std::string particle_path;
    float override_alpha = 1.0f;
    float override_rate = 1.0f;
};

struct ParticleEmitterConfig {
    std::string type;
    vec3 origin = {0, 0, 0};
    vec3 distance_max = {0, 0, 0};
    float distance_min = 0.0f;
    float rate = 0.0f;
};

struct ParticleInitializerConfig {
    std::string type;
    vec3 minimum = {0, 0, 0};
    vec3 maximum = {0, 0, 0};
    float minimum_scalar = 0.0f;
    float maximum_scalar = 0.0f;
};

struct ParticleOperatorConfig {
    std::string type;
    vec3 gravity = {0, 0, 0};
    float drag = 0.0f;
    float fade_in_time = 0.0f;
    float fade_out_time = 0.0f;
    float frequency_min = 0.0f;
    float frequency_max = 0.0f;
    float scale_min = 0.0f;
    float scale_max = 0.0f;
    float speed_min = 0.0f;
    float speed_max = 0.0f;
};

struct ParticleSystemConfig {
    std::string material_path;
    int max_particles = 100;
    bool additive = false;
    float start_time = 0.0f;
    std::vector<ParticleEmitterConfig> emitters;
    std::vector<ParticleInitializerConfig> initializers;
    std::vector<ParticleOperatorConfig> operators;
    std::vector<ParticleObjectConfig> children;
};

// Parses the scalar/vector value forms accepted by particle JSON files.
class ParticleParser {
   public:
    static void readVec3(const cJSON* node, vec3 out);
    static float readFloat(const cJSON* node);
    static ParticleObjectConfig parseObject(const wallpaper_engine::SceneObjectDocument& document);
    static ParticleObjectConfig parseObject(const cJSON* document);
    static ParticleSystemConfig parse(const cJSON* document);
};

#endif  // PARTICLE_PARSER_H
