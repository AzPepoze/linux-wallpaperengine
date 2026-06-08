#include "particles.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/context.h"
#include "../../core/logger.h"
#include "../../render/render.h"
#include "imgui.h"

#define TAG "PARTICLE"

static float rand_f() {
    return (float)rand() / (float)RAND_MAX;
}

static void parse_vec3(cJSON* node, vec3 out) {
    if (cJSON_IsString(node)) {
        sscanf(node->valuestring, "%f %f %f", &out[0], &out[1], &out[2]);
    } else if (cJSON_IsNumber(node)) {
        out[0] = out[1] = out[2] = (float)node->valuedouble;
    } else {
        out[0] = out[1] = out[2] = 0.0f;
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

    if (texture.id == SG_INVALID_ID) {
        LOG_TAG_W(TAG, "Particle system initialized with INVALID texture");
    }
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

    // Default Emitter logic
    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    cJSON* emitter;
    cJSON_ArrayForEach(emitter, emitters) {
        cJSON* type_node = cJSON_GetObjectItemCaseSensitive(emitter, "name");
        if (!type_node) continue;
        const char* type = type_node->valuestring;

        vec3 origin = {0, 0, 0};
        parse_vec3(cJSON_GetObjectItemCaseSensitive(emitter, "origin"), origin);

        if (strcmp(type, "sphererandom") == 0) {
            float dmin = get_float(cJSON_GetObjectItemCaseSensitive(emitter, "distancemin"));
            float dmax = get_float(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"));
            float dist = dmin + rand_f() * (dmax - dmin);
            float angle = rand_f() * 2.0f * M_PI;
            p.position[0] = origin[0] + cosf(angle) * dist;
            p.position[1] = origin[1] + sinf(angle) * dist;
        } else if (strcmp(type, "boxrandom") == 0) {
            vec3 dmax = {0, 0, 0};
            parse_vec3(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"), dmax);
            p.position[0] = origin[0] + (rand_f() * 2.0f - 1.0f) * dmax[0];
            p.position[1] = origin[1] + (rand_f() * 2.0f - 1.0f) * dmax[1];
        } else {
            p.position[0] = origin[0];
            p.position[1] = origin[1];
        }
    }

    // Initializers
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
        } else if (strcmp(name, "colorrandom") == 0) {
            vec3 min, max;
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "min"), min);
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "max"), max);
            p.color[0] = (min[0] + rand_f() * (max[0] - min[0])) / 255.0f;
            p.color[1] = (min[1] + rand_f() * (max[1] - min[1])) / 255.0f;
            p.color[2] = (min[2] + rand_f() * (max[2] - min[2])) / 255.0f;
        } else if (strcmp(name, "alpharandom") == 0) {
            float min = get_float(cJSON_GetObjectItemCaseSensitive(init, "min"));
            float max = get_float(cJSON_GetObjectItemCaseSensitive(init, "max"));
            p.alpha = min + rand_f() * (max - min);
            p.initial_alpha = p.alpha;
        } else if (strcmp(name, "rotationrandom") == 0) {
            p.rotation = rand_f() * 360.0f;
        } else if (strcmp(name, "angularvelocityrandom") == 0) {
            vec3 min, max;
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "min"), min);
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "max"), max);
            p.angular_vel = min[2] + rand_f() * (max[2] - min[2]);
        }
    }

    // Operators (pre-set some properties)
    cJSON* operators = cJSON_GetObjectItemCaseSensitive(config, "operator");
    cJSON* op;
    cJSON_ArrayForEach(op, operators) {
        cJSON* name_node = cJSON_GetObjectItemCaseSensitive(op, "name");
        if (!name_node) continue;
        const char* name = name_node->valuestring;

        if (strcmp(name, "movement") == 0) {
            parse_vec3(cJSON_GetObjectItemCaseSensitive(op, "gravity"), p.gravity);
            p.drag = get_float(cJSON_GetObjectItemCaseSensitive(op, "drag"));
        } else if (strcmp(name, "alphafade") == 0) {
            p.fade_in = get_float(cJSON_GetObjectItemCaseSensitive(op, "fadeintime"));
            p.fade_out = get_float(cJSON_GetObjectItemCaseSensitive(op, "fadeouttime"));
            if (p.fade_out == 0) p.fade_out = 1.0f;
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

        // Apply movement
        p.velocity[0] += p.gravity[0] * dt;
        p.velocity[1] += p.gravity[1] * dt;
        p.velocity[2] += p.gravity[2] * dt;

        if (p.drag > 0) {
            p.velocity[0] *= (1.0f - p.drag);
            p.velocity[1] *= (1.0f - p.drag);
            p.velocity[2] *= (1.0f - p.drag);
        }

        p.position[0] += p.velocity[0] * dt;
        p.position[1] += p.velocity[1] * dt;
        p.position[2] += p.velocity[2] * dt;
        p.rotation += p.angular_vel * dt;

        // Apply alpha fade
        float age = p.max_life - p.life;
        float alpha = p.initial_alpha;
        if (age < p.fade_in) {
            alpha *= (age / p.fade_in);
        }
        if (p.life < p.fade_out) {
            alpha *= (p.life / p.fade_out);
        }
        p.alpha = alpha;
    }
}

void ParticleSystem::draw() {
    if (show_bounds) drawDebugBounds();
    if (texture.id == SG_INVALID_ID) return;

    for (auto& p : particles) {
        float tint[4] = {p.color[0], p.color[1], p.color[2], p.alpha};

        // Align coordinates with Scene center
        float rx = state.offset_x + (state.scene_w * 0.5f + p.position[0]) * state.render_scale;
        float ry = state.offset_y + (state.scene_h * 0.5f - p.position[1]) * state.render_scale;
        float rs = p.size * state.render_scale;

        // Origin at center of sprite
        renderer_draw_sprite(&state.renderer, texture, rx - rs * 0.5f, ry - rs * 0.5f, rs, rs, p.rotation, tint);
    }
}

void ParticleSystem::drawDebugBounds() {
    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    cJSON* emitter;
    cJSON_ArrayForEach(emitter, emitters) {
        cJSON* type_node = cJSON_GetObjectItemCaseSensitive(emitter, "name");
        if (!type_node) continue;
        const char* type = type_node->valuestring;

        vec3 origin = {0, 0, 0};
        parse_vec3(cJSON_GetObjectItemCaseSensitive(emitter, "origin"), origin);

        float color[4] = {1.0f, 1.0f, 0.0f, 0.3f};  // Yellow semi-transparent

        if (strcmp(type, "boxrandom") == 0) {
            vec3 dmax = {0, 0, 0};
            parse_vec3(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"), dmax);

            float bx = state.offset_x + (state.scene_w * 0.5f + origin[0] - dmax[0]) * state.render_scale;
            float by = state.offset_y + (state.scene_h * 0.5f - origin[1] - dmax[1]) * state.render_scale;
            float bw = dmax[0] * 2.0f * state.render_scale;
            float bh = dmax[1] * 2.0f * state.render_scale;
            renderer_draw_rect(&state.renderer, bx, by, bw, bh, color);
        } else if (strcmp(type, "sphererandom") == 0) {
            float dmax = get_float(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"));
            float bx = state.offset_x + (state.scene_w * 0.5f + origin[0] - dmax) * state.render_scale;
            float by = state.offset_y + (state.scene_h * 0.5f - origin[1] - dmax) * state.render_scale;
            float bw = dmax * 2.0f * state.render_scale;
            float bh = dmax * 2.0f * state.render_scale;
            renderer_draw_rect(&state.renderer, bx, by, bw, bh, color);
        }
    }
}

void ParticleSystem::showInspector() {
    ImGui::Text("Active Particles: %d / %d", (int)particles.size(), max_particles);
    if (!config_path.empty()) {
        ImGui::Text("Config: %s", config_path.c_str());
    }
    if (!texture_path.empty()) {
        ImGui::Text("Texture: %s", texture_path.c_str());
    }
    if (texture.id == SG_INVALID_ID) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "TEXTURE MISSING");
    }

    ImGui::Checkbox("Show Spawning Bounds", &show_bounds);
}
