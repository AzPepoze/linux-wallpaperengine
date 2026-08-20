#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <string>
#include <vector>

#include "cJSON.h"
#include "core/gfx_resource.h"
#include "core/interfaces.h"
#include "linmath.h"
#include "sokol_gfx.h"

struct Particle {
    vec3 position = {0, 0, 0};
    vec3 base_position = {0, 0, 0};
    vec3 velocity = {0, 0, 0};
    vec3 color = {1, 1, 1};
    float life = 0.0f;
    float max_life = 0.0f;
    float alpha = 1.0f;
    float initial_alpha = 1.0f;
    float rotation = 0.0f;
    float angular_vel = 0.0f;
    float size = 1.0f;
    float initial_size = 1.0f;
    float spawn_time = 0.0f;
    float random_seed = 0.0f;

    float drag = 0.0f;
    vec3 gravity = {0, 0, 0};
    float fade_in = 0.0f;
    float fade_out = 1.0f;

    float osc_alpha_freq = 0.0f;
    float osc_alpha_min = 1.0f;
    float osc_size_freq = 0.0f;
    float osc_size_min = 1.0f;
    float osc_size_max = 1.0f;
    float osc_pos_freq = 0.0f;
    float osc_pos_min = 0.0f;
    float osc_pos_max = 0.0f;

    float turb_speed = 0.0f;
};

class EngineContext;

class ParticleSystem {
   public:
    std::string name;
    cJSON* config;
    GfxImage texture;
    GfxView cached_view;
    std::vector<Particle> particles;
    std::vector<ParticleSystem*> children;

    int max_particles;
    std::vector<float> emitter_timers;
    float global_time = 0.0f;
    float scene_w, scene_h;
    vec3 layer_origin = {0, 0, 0};
    vec2 parallax = {0, 0};
    bool is_additive = false;

    float override_alpha = 1.0f;
    float override_rate = 1.0f;

    ParticleSystem(cJSON* config, GfxImage tex, float sw, float h);
    ~ParticleSystem();

    static ParticleSystem* createFromJSON(cJSON* node, const IAssetResolver& assets, float sw, float sh);
    static ParticleSystem* createFromPath(const char* particle_path, const IAssetResolver& assets, float sw, float sh,
                                          float override_alpha = 1.0f, float override_rate = 1.0f);

    void update(float dt);
    void draw(EngineContext& ctx);
    void drawDebugBounds(EngineContext& ctx);

    bool show_bounds = false;
    std::string config_path;
    std::string texture_path;

   private:
    void spawnParticle();
};

#endif  // PARTICLE_SYSTEM_H
