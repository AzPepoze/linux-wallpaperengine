#include "particles.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../libs/sokol/sokol_app.h"
#include "../../core/context.h"
#include "../../core/logger.h"
#include "../../core/utils.h"
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
    if (!node) return 0.0f;
    if (cJSON_IsNumber(node)) return (float)node->valuedouble;
    if (cJSON_IsString(node)) return (float)atof(node->valuestring);
    return 0.0f;
}

ParticleSystem::ParticleSystem(cJSON* config, sg_image tex, float sw, float sh)
    : config(config), texture(tex), scene_w(sw), scene_h(sh) {
    cJSON* max_count = cJSON_GetObjectItemCaseSensitive(config, "maxcount");
    max_particles = cJSON_IsNumber(max_count) ? max_count->valueint : 100;
    particles.reserve(max_particles);

    // Blending
    cJSON* passes = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(passes)) {
        cJSON* pass = cJSON_GetArrayItem(passes, 0);
        cJSON* blend = cJSON_GetObjectItemCaseSensitive(pass, "blending");
        if (cJSON_IsString(blend) && strcmp(blend->valuestring, "additive") == 0) {
            is_additive = true;
        }
    }

    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    int emitter_count = cJSON_GetArraySize(emitters);
    emitter_timers.resize(emitter_count, 0.0f);

    // Load Children
    cJSON* children_node = cJSON_GetObjectItemCaseSensitive(config, "children");
    if (cJSON_IsArray(children_node)) {
        cJSON* child_json;
        cJSON_ArrayForEach(child_json, children_node) {
            ParticleSystem* child = ParticleSystem::createFromJSON(child_json, state.asset_mgr, sw, sh);
            if (child) children.push_back(child);
        }
    }

    // Initial Warmup
    float start_time = get_float(cJSON_GetObjectItemCaseSensitive(config, "starttime"));
    if (start_time > 0) {
        float step = 0.1f;
        for (float t = 0; t < start_time; t += step) {
            update(step);
        }
    }
}

ParticleSystem::~ParticleSystem() {
    if (config) cJSON_Delete(config);
    if (texture.id != SG_INVALID_ID) sg_destroy_image(texture);
    if (cached_view.id != SG_INVALID_ID) sg_destroy_view(cached_view);
    for (auto c : children) delete c;
    children.clear();
}

ParticleSystem* ParticleSystem::createFromJSON(cJSON* node, const class AssetManager& assets, float sw, float sh) {
    cJSON* p_file = cJSON_GetObjectItemCaseSensitive(node, "particle");
    if (!p_file) p_file = cJSON_GetObjectItemCaseSensitive(node, "name");
    if (!p_file) p_file = cJSON_GetObjectItemCaseSensitive(node, "file");

    if (p_file && cJSON_IsString(p_file)) {
        char p_abs[1024];
        if (assets.resolvePath(p_file->valuestring, p_abs, sizeof(p_abs))) {
            char* p_json_str = read_file_to_string(p_abs);
            if (p_json_str) {
                cJSON* p_json = cJSON_Parse(p_json_str);
                free(p_json_str);
                if (p_json) {
                    std::string p_tex_path;
                    sg_image p_tex = assets.resolveTexture("materials/particle.tex", &p_tex_path);
                    cJSON* mat = cJSON_GetObjectItemCaseSensitive(p_json, "material");
                    if (cJSON_IsString(mat)) {
                        sg_image mat_tex = assets.resolveMaterialTexture(mat->valuestring, &p_tex_path);
                        if (mat_tex.id != SG_INVALID_ID) p_tex = mat_tex;
                    }

                    ParticleSystem* ps = new ParticleSystem(p_json, p_tex, sw, sh);
                    ps->config_path = p_abs;
                    ps->texture_path = p_tex_path;

                    cJSON* override = cJSON_GetObjectItemCaseSensitive(node, "instanceoverride");
                    if (cJSON_IsObject(override)) {
                        cJSON* alpha = cJSON_GetObjectItemCaseSensitive(override, "alpha");
                        if (cJSON_IsNumber(alpha)) ps->override_alpha = (float)alpha->valuedouble;
                        cJSON* rate = cJSON_GetObjectItemCaseSensitive(override, "rate");
                        if (cJSON_IsNumber(rate)) ps->override_rate = (float)rate->valuedouble;
                    }

                    return ps;
                }
            }
        }
    }
    return nullptr;
}

void ParticleSystem::spawnParticle() {
    if ((int)particles.size() >= max_particles) return;

    Particle p = {};
    p.random_seed = rand_f();
    p.spawn_time = global_time;
    p.alpha = 1.0f;
    p.initial_alpha = 1.0f;
    p.color[0] = 1.0f;
    p.color[1] = 1.0f;
    p.color[2] = 1.0f;

    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    cJSON* emitter;
    cJSON_ArrayForEach(emitter, emitters) {
        const char* type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(emitter, "name"));
        if (!type) continue;
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

    cJSON* initializers = cJSON_GetObjectItemCaseSensitive(config, "initializer");
    cJSON* init;
    cJSON_ArrayForEach(init, initializers) {
        const char* name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(init, "name"));
        if (!name) continue;
        if (strcmp(name, "lifetimerandom") == 0) {
            p.max_life = get_float(cJSON_GetObjectItemCaseSensitive(init, "min")) +
                         rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(init, "max")) -
                                     get_float(cJSON_GetObjectItemCaseSensitive(init, "min")));
            p.life = p.max_life;
        } else if (strcmp(name, "sizerandom") == 0) {
            p.size = get_float(cJSON_GetObjectItemCaseSensitive(init, "min")) +
                     rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(init, "max")) -
                                 get_float(cJSON_GetObjectItemCaseSensitive(init, "min")));
            p.initial_size = p.size;
        } else if (strcmp(name, "velocityrandom") == 0) {
            vec3 min, max;
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "min"), min);
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "max"), max);
            p.velocity[0] = min[0] + rand_f() * (max[0] - min[0]);
            p.velocity[1] = min[1] + rand_f() * (max[1] - min[1]);
        } else if (strcmp(name, "colorrandom") == 0) {
            vec3 min, max;
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "min"), min);
            parse_vec3(cJSON_GetObjectItemCaseSensitive(init, "max"), max);
            p.color[0] = (min[0] + rand_f() * (max[0] - min[0])) / 255.0f;
            p.color[1] = (min[1] + rand_f() * (max[1] - min[1])) / 255.0f;
            p.color[2] = (min[2] + rand_f() * (max[2] - min[2])) / 255.0f;
        } else if (strcmp(name, "alpharandom") == 0) {
            p.alpha = get_float(cJSON_GetObjectItemCaseSensitive(init, "min")) +
                      rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(init, "max")) -
                                  get_float(cJSON_GetObjectItemCaseSensitive(init, "min")));
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
    p.base_position[0] = p.position[0];
    p.base_position[1] = p.position[1];

    cJSON* operators = cJSON_GetObjectItemCaseSensitive(config, "operator");
    cJSON* op;
    cJSON_ArrayForEach(op, operators) {
        const char* name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(op, "name"));
        if (!name) continue;
        if (strcmp(name, "movement") == 0) {
            parse_vec3(cJSON_GetObjectItemCaseSensitive(op, "gravity"), p.gravity);
            p.drag = get_float(cJSON_GetObjectItemCaseSensitive(op, "drag"));
        } else if (strcmp(name, "alphafade") == 0) {
            p.fade_in = get_float(cJSON_GetObjectItemCaseSensitive(op, "fadeintime"));
            p.fade_out = get_float(cJSON_GetObjectItemCaseSensitive(op, "fadeouttime"));
            if (p.fade_out == 0) p.fade_out = 1.0f;
        } else if (strcmp(name, "oscillatealpha") == 0) {
            p.osc_alpha_freq = get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymin")) +
                               rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymax")) -
                                           get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymin")));
            p.osc_alpha_min = get_float(cJSON_GetObjectItemCaseSensitive(op, "scalemin"));
        } else if (strcmp(name, "oscillatesize") == 0) {
            p.osc_size_freq = get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymin")) +
                              rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymax")) -
                                          get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymin")));
            p.osc_size_min = get_float(cJSON_GetObjectItemCaseSensitive(op, "scalemin"));
            p.osc_size_max = get_float(cJSON_GetObjectItemCaseSensitive(op, "scalemax"));
        } else if (strcmp(name, "oscillateposition") == 0) {
            p.osc_pos_freq = get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymin")) +
                             rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymax")) -
                                         get_float(cJSON_GetObjectItemCaseSensitive(op, "frequencymin")));
            p.osc_pos_min = get_float(cJSON_GetObjectItemCaseSensitive(op, "scalemin"));
            p.osc_pos_max = get_float(cJSON_GetObjectItemCaseSensitive(op, "scalemax"));
        } else if (strcmp(name, "turbulence") == 0) {
            p.turb_speed = get_float(cJSON_GetObjectItemCaseSensitive(op, "speedmin")) +
                           rand_f() * (get_float(cJSON_GetObjectItemCaseSensitive(op, "speedmax")) -
                                       get_float(cJSON_GetObjectItemCaseSensitive(op, "speedmin")));
        }
    }
    particles.push_back(p);
}

void ParticleSystem::update(float dt) {
    global_time += dt;
    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    cJSON* emitter;
    int i = 0;
    cJSON_ArrayForEach(emitter, emitters) {
        float rate = get_float(cJSON_GetObjectItemCaseSensitive(emitter, "rate")) * override_rate;
        if (rate > 0) {
            emitter_timers[i] += dt;
            float interval = 1.0f / rate;
            while (emitter_timers[i] >= interval) {
                spawnParticle();
                emitter_timers[i] -= interval;
            }
        }
        i++;
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
        p.velocity[0] += p.gravity[0] * dt;
        p.velocity[1] += p.gravity[1] * dt;
        if (p.drag > 0) {
            p.velocity[0] *= (1.0f - p.drag * dt);
            p.velocity[1] *= (1.0f - p.drag * dt);
        }
        if (p.turb_speed > 0) {
            float angle = p.random_seed * 2.0f * M_PI + global_time * 2.0f;
            p.velocity[0] += cosf(angle) * p.turb_speed * dt;
            p.velocity[1] += sinf(angle) * p.turb_speed * dt;
        }
        p.base_position[0] += p.velocity[0] * dt;
        p.base_position[1] += p.velocity[1] * dt;
        p.rotation += p.angular_vel * dt;
        p.position[0] = p.base_position[0];
        p.position[1] = p.base_position[1];
        if (p.osc_pos_freq > 0) {
            float wave = sinf(global_time * p.osc_pos_freq + p.random_seed * 100.0f);
            float amp = p.osc_pos_min + (p.osc_pos_max - p.osc_pos_min) * 0.5f;
            p.position[0] += wave * amp;
            p.position[1] += cosf(global_time * p.osc_pos_freq) * amp;
        }
        float age = p.max_life - p.life;
        float alpha = p.initial_alpha * override_alpha;
        if (age < p.fade_in) alpha *= (age / p.fade_in);
        if (p.life < p.fade_out) alpha *= (p.life / p.fade_out);
        if (p.osc_alpha_freq > 0) {
            float wave = (sinf(global_time * p.osc_alpha_freq + p.random_seed * 10.0f) + 1.0f) * 0.5f;
            alpha *= (p.osc_alpha_min + wave * (1.0f - p.osc_alpha_min));
        }
        p.alpha = alpha;
        float size = p.initial_size;
        if (p.osc_size_freq > 0) {
            float wave = (sinf(global_time * p.osc_size_freq) + 1.0f) * 0.5f;
            size *= (p.osc_size_min + wave * (p.osc_size_max - p.osc_size_min));
        }
        p.size = size;
    }
    for (auto c : children) c->update(dt);
}

void ParticleSystem::draw() {
    if (texture.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) {
        sg_view_desc v_desc = {};
        v_desc.texture.image = texture;
        cached_view = sg_make_view(&v_desc);
    }
    float px = parallax[0] * state.parallax_smooth_x * 50.0f;
    float py = parallax[1] * state.parallax_smooth_y * 50.0f;
    for (auto& p : particles) {
        float tint[4] = {p.color[0], p.color[1], p.color[2], p.alpha};
        float rx = state.offset_x + (layer_origin[0] + px + p.position[0]) * state.render_scale;
        float ry = state.offset_y + (layer_origin[1] + py + p.position[1]) * state.render_scale;
        float rs = p.size * state.render_scale;
        renderer_draw_sprite(&state.renderer, texture, cached_view, rx - rs * 0.5f, ry - rs * 0.5f, rs, rs, p.rotation,
                             tint, is_additive, nullptr);
    }
    for (auto c : children) {
        c->layer_origin[0] = layer_origin[0];
        c->layer_origin[1] = layer_origin[1];
        c->parallax[0] = parallax[0];
        c->parallax[1] = parallax[1];
        c->draw();
    }
}

void ParticleSystem::drawDebugBounds() {
    cJSON* emitters = cJSON_GetObjectItemCaseSensitive(config, "emitter");
    cJSON* emitter;
    float px = parallax[0] * state.parallax_smooth_x * 50.0f;
    float py = parallax[1] * state.parallax_smooth_y * 50.0f;
    cJSON_ArrayForEach(emitter, emitters) {
        const char* type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(emitter, "name"));
        if (!type) continue;
        vec3 origin = {0, 0, 0};
        parse_vec3(cJSON_GetObjectItemCaseSensitive(emitter, "origin"), origin);
        float color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        if (strcmp(type, "boxrandom") == 0) {
            vec3 dmax = {0, 0, 0};
            parse_vec3(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"), dmax);
            float bx = state.offset_x + (layer_origin[0] + px + origin[0] - dmax[0]) * state.render_scale;
            float by = state.offset_y + (layer_origin[1] + py + origin[1] - dmax[1]) * state.render_scale;
            renderer_draw_rect(&state.renderer, bx, by, dmax[0] * 2.0f * state.render_scale,
                               dmax[1] * 2.0f * state.render_scale, color);
        } else if (strcmp(type, "sphererandom") == 0) {
            float dmax = get_float(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"));
            float bx = state.offset_x + (layer_origin[0] + px + origin[0] - dmax) * state.render_scale;
            float by = state.offset_y + (layer_origin[1] + py + origin[1] - dmax) * state.render_scale;
            renderer_draw_rect(&state.renderer, bx, by, dmax * 2.0f * state.render_scale,
                               dmax * 2.0f * state.render_scale, color);
        }
    }

    // Real-time Particle Tracking (draw boxes for each particle)
    float p_color[4] = {1, 0, 0, 1};
    for (auto& p : particles) {
        float rx = state.offset_x + (layer_origin[0] + px + p.position[0]) * state.render_scale;
        float ry = state.offset_y + (layer_origin[1] + py + p.position[1]) * state.render_scale;
        float rs = p.size * state.render_scale;
        renderer_draw_rect(&state.renderer, rx - rs * 0.5f, ry - rs * 0.5f, rs, rs, p_color);
    }

    for (auto c : children) c->drawDebugBounds();
}

void ParticleSystem::showInspector() {
    ImGui::Text("Active Particles: %d / %d", (int)particles.size(), max_particles);
    if (!config_path.empty()) ImGui::Text("Config: %s", config_path.c_str());
    if (!texture_path.empty()) ImGui::Text("Texture: %s", texture_path.c_str());
    ImGui::Text("Blending: %s", is_additive ? "Additive" : "Alpha");
    ImGui::Text("Children: %d", (int)children.size());
}
