#ifndef PARTICLE_PARSER_H
#define PARTICLE_PARSER_H

#include <cjson/cJSON.h>

#include <string>
#include <vector>

#include "linmath.h"
#include "wallpaper/2d/parser/scene_document.h"

struct ParticleObjectConfig {
    std::string name;
    std::string particle_path;
    float override_alpha = 1.0f;
    float override_rate = 1.0f;
    float override_size = 1.0f;
    float override_count = 1.0f;
    float override_speed = 1.0f;
    float override_lifetime = 1.0f;
    vec3 override_color = {1.0f, 1.0f, 1.0f};
    bool has_override_color = false;
    bool override_color_is_legacy = false;

    // Child system type: static | eventfollow | eventspawn | eventdeath.
    std::string type = "static";
    vec3 origin = {0.0f, 0.0f, 0.0f};
    vec3 angles = {0.0f, 0.0f, 0.0f};
    vec3 scale = {1.0f, 1.0f, 1.0f};
    int maxcount = 20;
    float probability = 1.0f;
    int controlpoint_start_index = 0;
};

struct ParticleEmitterConfig {
    std::string type;
    vec3 origin = {0, 0, 0};
    vec3 distance_max = {256, 256, 256};
    float distance_min = 0.0f;
    float rate = 5.0f;  // emissions per second when the emitter omits it
    // Particles emitted at once whenever the emitting instance has none alive (event child systems).
    int instantaneous = 0;
    int flags = 0;
    int control_point = 0;
};

struct ParticleControlPointConfig {
    int flags = 0;
    vec3 offset = {0, 0, 0};
};

struct ParticleInitializerConfig {
    std::string type;
    vec3 minimum = {0, 0, 0};
    vec3 maximum = {0, 0, 0};
    float minimum_scalar = 0.0f;
    float maximum_scalar = 0.0f;
    // Random values are drawn as pow(random, exponent) between min and max.
    float exponent = 1.0f;
    float turbulence_offset = 0.0f;
    float turbulence_scale = 1.0f;
    float turbulence_speed_min = 100.0f;
    float turbulence_speed_max = 250.0f;
    vec3 turbulence_forward = {0.0f, 1.0f, 0.0f};
};

struct ParticleOperatorConfig {
    std::string type;
    vec3 gravity = {0, 0, 0};
    float drag = 0.0f;
    // Alpha fades in lifetime fractions; both default to 0.5 when omitted.
    float fade_in_time = 0.5f;
    float fade_out_time = 0.5f;
    float frequency_min = 0.0f;
    float frequency_max = 0.0f;
    float scale_min = 0.0f;
    float scale_max = 0.0f;
    float speed_min = 0.0f;
    float speed_max = 0.0f;
    // Ramps over lifetime fractions: scalar for size and alpha, per channel for color.
    float change_start_time = 0.0f;
    float change_end_time = 1.0f;
    float change_start_value = 1.0f;
    float change_end_value = 0.0f;
    vec3 change_start_color = {1.0f, 1.0f, 1.0f};
    vec3 change_end_color = {0.0f, 0.0f, 0.0f};
    // angularmovement: angular acceleration (the z component spins a 2D sprite); `drag` slows the rotation.
    vec3 angular_force = {0, 0, 0};
    // controlpointattract: a pull (positive scale) or push (negative) towards a control point within `threshold`.
    int attract_control_point = 0;
    int attract_flags = 2;  // bit 2 limits the velocity change close to the control point
    float attract_scale = 512.0f;
    float attract_threshold = 512.0f;
    vec3 attract_origin = {0, 0, 0};
};

struct ParticleRendererConfig {
    std::string type = "sprite";
    float length = 0.0f;
    float max_length = 0.0f;
    int segments = 10;
};

struct ParticleSystemConfig {
    std::string material_path;
    std::string animation_mode = "sequence";
    float sequence_multiplier = 1.0f;
    int max_particles = 100;
    int flags = 0;
    bool additive = false;
    float start_time = 0.0f;
    ParticleRendererConfig renderer;
    std::vector<ParticleEmitterConfig> emitters;
    std::vector<ParticleInitializerConfig> initializers;
    std::vector<ParticleOperatorConfig> operators;
    std::vector<ParticleObjectConfig> children;
    ParticleControlPointConfig control_points[8];
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
