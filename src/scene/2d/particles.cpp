#include "particles.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/context.h"
#include "../../core/logger.h"
#include "../../render/render.h"
#include "imgui.h"

static float rand_f() {
    return (float)rand() / (float)RAND_MAX;
}

static void parse_vec3(cJSON* node, vec3 out) {
    if (cJSON_IsString(node)) {
        sscanf(node->valuestring, "%f %f %f", &out[0], &out[1], &out[2]);
    } else if (cJSON_IsNumber(node)) {
        out[0] = out[1] = out[2] = (float)node->valuedouble;
    }
}

static float get_float(cJSON* node) {
    if (cJSON_IsNumber(node)) return (float)node->valuedouble;
    if (cJSON_IsString(node)) return (float)atof(node->valuestring);
    return 0.0f;
}

ParticleSystem::ParticleSystem(cJSON* config, sg_image tex, float sw, float sh)
    : config(config), texture(tex), scene_w(sw), scene_h(sh) {
    cJSON* max_count = cJSON_GetObjectItemCaseSensitive(config, "maxcount");
    max_particles = cJSON_IsNumber(max_count) ? max_count->valueint : 100;
    particles.reserve(max_particles);
}

ParticleSystem::~ParticleSystem() {
    // config belongs to scene_loader
}

void ParticleSystem::spawnParticle() {
    if ((int)particles.size() >= max_particles) return;

    Particle p = {};
    p.random_value = rand_f();
    p.spawn_time = global_time;
    p.alpha = 1.0f;
    p.initial_alpha = 1.0f;
    p.color[0] = 1.0f;
    p.color[1] = 1.0f;
    p.color[2] = 1.0f;

    cJSON* initializers = cJSON_GetObjectItemCaseSensitive(config, "initializer");
    cJSON* init;
    cJSON_ArrayForEach(init, initializers) {
        cJSON* name_node = cJSON_GetObjectItemCaseSensitive(init, "name");
        if (!name_node) continue;
        const char* name = name_node->valuestring;

        if (strcmp(name, "lifetimerandom") == 0) {
            float min = get_float(cJSON_GetObjectItemCaseSensitive(init, "min"));
            float max = get_float(cJSON_GetObjectItemCaseSensitive(init, "max"));
            p.max_life = min + rand_f() * (max - min);
            p.life = p.max_life;
        } else if (strcmp(name, "sizerandom") == 0) {
            float min = get_float(cJSON_GetObjectItemCaseSensitive(init, "min"));
            float max = get_float(cJSON_GetObjectItemCaseSensitive(init, "max"));
            p.size = min + rand_f() * (max - min);
            p.initial_size = p.size;
        } else if (strcmp(name, "velocityrandom") == 0) {
            vec3 min, max;
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "min"), min);
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "max"), max);
            p.velocity[0] = min[0] + rand_f() * (max[0] - min[0]);
            p.velocity[1] = min[1] + rand_f() * (max[1] - min[1]);
            p.velocity[2] = min[2] + rand_f() * (max[2] - min[2]);
        }
    }
    particles.push_back(p);
}

void ParticleSystem::update(float dt) {
    global_time += dt;

    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    cJSON* emitter;
    cJSON_ArrayForEach(emitter, emitters) {
        float rate = get_float(cJSON_GetObjectItemCaseSensitive(emitter, "rate"));
        if (rate > 0) {
            timer += dt;
            float interval = 1.0f / rate;
            while (timer >= interval) {
                spawnParticle();
                timer -= interval;
            }
        }
    }

    for (size_t i = 0; i < particles.size(); i++) {
        Particle& p = particles[i];
        p.life -= dt;
        if (p.life <= 0) {
            particles[i] = particles.back();
            particles.pop_back();
            i--;
            continue;
        }

        p.position[0] += p.velocity[0] * dt;
        p.position[1] += p.velocity[1] * dt;
        p.position[2] += p.velocity[2] * dt;
        p.alpha = p.life / p.max_life;
    }
}

void ParticleSystem::draw() {
    if (texture.id == SG_INVALID_ID) return;

    for (auto& p : particles) {
        float tint[4] = {p.color[0], p.color[1], p.color[2], p.alpha};
        float rx = state.offset_x + p.position[0] * state.render_scale;
        float ry = state.offset_y + p.position[1] * state.render_scale;
        float rs = p.size * state.render_scale;
        renderer_draw_sprite(&state.renderer, texture, rx, ry, rs, rs, p.rotation, tint);
    }
}

void ParticleSystem::showInspector() {
    ImGui::Text("Active Particles: %d / %d", (int)particles.size(), max_particles);
}
