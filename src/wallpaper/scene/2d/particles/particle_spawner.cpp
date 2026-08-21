#include <math.h>
#include <stdlib.h>

#include "particle_system.h"

namespace {
float randomFloat() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}
}  // namespace

void ParticleSystem::spawnParticle() {
    if (static_cast<int>(particles.size()) >= max_particles) return;
    Particle particle = {};
    particle.random_seed = randomFloat();
    particle.spawn_time = global_time;
    particle.alpha = 1.0f;
    particle.initial_alpha = 1.0f;
    particle.color[0] = particle.color[1] = particle.color[2] = 1.0f;
    if (spritesheet_frames > 1 && config.animation_mode == "randomframe") {
        particle.frame = floorf(randomFloat() * (float)spritesheet_frames);
        if (particle.frame >= spritesheet_frames) particle.frame = (float)(spritesheet_frames - 1);
    } else if (spritesheet_frames > 1) {
        particle.frame = 0.0f;
    }

    for (const ParticleEmitterConfig& emitter : config.emitters) {
        if (emitter.type == "sphererandom") {
            const float distance =
                emitter.distance_min + randomFloat() * (emitter.distance_max[0] - emitter.distance_min);
            const float angle = randomFloat() * 2.0f * M_PI;
            particle.position[0] = emitter.origin[0] + cosf(angle) * distance;
            particle.position[1] = emitter.origin[1] + sinf(angle) * distance;
        } else if (emitter.type == "boxrandom") {
            particle.position[0] = emitter.origin[0] + (randomFloat() * 2.0f - 1.0f) * emitter.distance_max[0];
            particle.position[1] = emitter.origin[1] + (randomFloat() * 2.0f - 1.0f) * emitter.distance_max[1];
        } else {
            particle.position[0] = emitter.origin[0];
            particle.position[1] = emitter.origin[1];
        }
    }
    for (const ParticleInitializerConfig& initializer : config.initializers) {
        if (initializer.type == "lifetimerandom") {
            particle.max_life =
                initializer.minimum_scalar + randomFloat() * (initializer.maximum_scalar - initializer.minimum_scalar);
            particle.life = particle.max_life;
        } else if (initializer.type == "sizerandom") {
            particle.size =
                initializer.minimum_scalar + randomFloat() * (initializer.maximum_scalar - initializer.minimum_scalar);
            particle.initial_size = particle.size;
        } else if (initializer.type == "velocityrandom") {
            particle.velocity[0] =
                initializer.minimum[0] + randomFloat() * (initializer.maximum[0] - initializer.minimum[0]);
            particle.velocity[1] =
                initializer.minimum[1] + randomFloat() * (initializer.maximum[1] - initializer.minimum[1]);
        } else if (initializer.type == "colorrandom") {
            for (int component = 0; component < 3; ++component)
                particle.color[component] =
                    (initializer.minimum[component] +
                     randomFloat() * (initializer.maximum[component] - initializer.minimum[component])) /
                    255.0f;
        } else if (initializer.type == "alpharandom") {
            particle.alpha =
                initializer.minimum_scalar + randomFloat() * (initializer.maximum_scalar - initializer.minimum_scalar);
            particle.initial_alpha = particle.alpha;
        } else if (initializer.type == "rotationrandom") {
            particle.rotation =
                initializer.minimum[2] + randomFloat() * (initializer.maximum[2] - initializer.minimum[2]);
        } else if (initializer.type == "angularvelocityrandom") {
            particle.angular_vel =
                initializer.minimum[2] + randomFloat() * (initializer.maximum[2] - initializer.minimum[2]);
        } else if (initializer.type == "turbulentvelocityrandom") {
            // Wallpaper Engine seeds this initializer with curl noise. The
            // compact runtime keeps its authored forward direction, random
            // phase and speed, which importantly establishes a valid trail
            // direction before the turbulence operator evolves it.
            const float phase = initializer.turbulence_offset +
                                (randomFloat() - 0.5f) * initializer.turbulence_scale;
            const float speed = initializer.turbulence_speed_min +
                                randomFloat() * (initializer.turbulence_speed_max - initializer.turbulence_speed_min);
            const float forward_x = initializer.turbulence_forward[0];
            const float forward_y = initializer.turbulence_forward[1];
            particle.velocity[0] += (forward_x * cosf(phase) - forward_y * sinf(phase)) * speed;
            particle.velocity[1] += (forward_x * sinf(phase) + forward_y * cosf(phase)) * speed;
        }
    }
    if (has_override_color) {
        for (int component = 0; component < 3; ++component) {
            const float authored_color = override_color[component] / (override_color_is_legacy ? 255.0f : 1.0f);
            // Wallpaper Engine converts UI particle colours to linear space at spawn.
            particle.color[component] = authored_color * authored_color;
        }
    }
    particle.base_position[0] = particle.position[0];
    particle.base_position[1] = particle.position[1];
    for (const ParticleOperatorConfig& particle_operator : config.operators) {
        if (particle_operator.type == "movement") {
            vec3_dup(particle.gravity, particle_operator.gravity);
            particle.drag = particle_operator.drag;
        } else if (particle_operator.type == "alphafade") {
            particle.fade_in = particle_operator.fade_in_time;
            particle.fade_out = particle_operator.fade_out_time == 0 ? 1.0f : particle_operator.fade_out_time;
        } else if (particle_operator.type == "oscillatealpha") {
            particle.osc_alpha_freq =
                particle_operator.frequency_min +
                randomFloat() * (particle_operator.frequency_max - particle_operator.frequency_min);
            particle.osc_alpha_min = particle_operator.scale_min;
        } else if (particle_operator.type == "oscillatesize") {
            particle.osc_size_freq =
                particle_operator.frequency_min +
                randomFloat() * (particle_operator.frequency_max - particle_operator.frequency_min);
            particle.osc_size_min = particle_operator.scale_min;
            particle.osc_size_max = particle_operator.scale_max;
        } else if (particle_operator.type == "oscillateposition") {
            particle.osc_pos_freq = particle_operator.frequency_min +
                                    randomFloat() * (particle_operator.frequency_max - particle_operator.frequency_min);
            particle.osc_pos_min = particle_operator.scale_min;
            particle.osc_pos_max = particle_operator.scale_max;
        } else if (particle_operator.type == "turbulence") {
            particle.turb_speed = particle_operator.speed_min +
                                  randomFloat() * (particle_operator.speed_max - particle_operator.speed_min);
        }
    }
    particles.push_back(particle);
}
