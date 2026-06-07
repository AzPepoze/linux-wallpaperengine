#ifndef PARTICLES_H
#define PARTICLES_H

#include <string>
#include <vector>

#include "../../libs/cJSON.h"
#include "../../libs/linmath.h"
#include "../../libs/sokol/sokol_gfx.h"

struct Particle {
    vec3 position = {0, 0, 0};
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
    float random_value = 0.0f;
};

class ParticleSystem {
   public:
    std::string name;
    cJSON* config;
    sg_image texture;
    std::vector<Particle> particles;
    int max_particles;
    float timer = 0.0f;
    float global_time = 0.0f;
    float scene_w, scene_h;

    ParticleSystem(cJSON* config, sg_image tex, float sw, float sh);
    ~ParticleSystem();

    void update(float dt);
    void draw();
    void showInspector();

   private:
    void spawnParticle();
};

#endif  // PARTICLES_H
