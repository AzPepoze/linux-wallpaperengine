#include <math.h>

#include "particle_system.h"

void ParticleSystem::update(float dt) {
    global_time += dt;
    for (size_t emitter_index = 0; emitter_index < config.emitters.size(); ++emitter_index) {
        const float rate = config.emitters[emitter_index].rate * override_rate;
        if (rate > 0) {
            emitter_timers[emitter_index] += dt;
            const float interval = 1.0f / rate;
            while (emitter_timers[emitter_index] >= interval) {
                spawnParticle();
                emitter_timers[emitter_index] -= interval;
            }
        }
    }
    for (size_t index = 0; index < particles.size(); ++index) {
        Particle& particle = particles[index];
        particle.life -= dt;
        if (particle.life <= 0) {
            particles[index] = particles.back();
            particles.pop_back();
            --index;
            continue;
        }
        particle.velocity[0] += particle.gravity[0] * dt;
        particle.velocity[1] += particle.gravity[1] * dt;
        if (particle.drag > 0) {
            particle.velocity[0] *= 1.0f - particle.drag * dt;
            particle.velocity[1] *= 1.0f - particle.drag * dt;
        }
        if (particle.turb_speed > 0) {
            const float angle = particle.random_seed * 2.0f * M_PI + global_time * 2.0f;
            particle.velocity[0] += cosf(angle) * particle.turb_speed * dt;
            particle.velocity[1] += sinf(angle) * particle.turb_speed * dt;
        }
        particle.base_position[0] += particle.velocity[0] * dt;
        particle.base_position[1] += particle.velocity[1] * dt;
        particle.rotation += particle.angular_vel * dt;
        particle.position[0] = particle.base_position[0];
        particle.position[1] = particle.base_position[1];
        if (particle.osc_pos_freq > 0) {
            const float wave = sinf(global_time * particle.osc_pos_freq + particle.random_seed * 100.0f);
            const float amplitude = particle.osc_pos_min + (particle.osc_pos_max - particle.osc_pos_min) * 0.5f;
            particle.position[0] += wave * amplitude;
            particle.position[1] += cosf(global_time * particle.osc_pos_freq) * amplitude;
        }
        const float age = particle.max_life - particle.life;
        float alpha = particle.initial_alpha * override_alpha;
        if (age < particle.fade_in) alpha *= age / particle.fade_in;
        if (particle.life < particle.fade_out) alpha *= particle.life / particle.fade_out;
        if (particle.osc_alpha_freq > 0) {
            const float wave =
                (sinf(global_time * particle.osc_alpha_freq + particle.random_seed * 10.0f) + 1.0f) * 0.5f;
            alpha *= particle.osc_alpha_min + wave * (1.0f - particle.osc_alpha_min);
        }
        particle.alpha = alpha;
        float size = particle.initial_size;
        if (particle.osc_size_freq > 0) {
            const float wave = (sinf(global_time * particle.osc_size_freq) + 1.0f) * 0.5f;
            size *= particle.osc_size_min + wave * (particle.osc_size_max - particle.osc_size_min);
        }
        particle.size = size;

        if (spritesheet_frames > 1 && config.animation_mode != "randomframe" && particle.max_life > 0.0f) {
            const float lifetime_pos = fmaxf(0.0f, fminf(1.0f, age / particle.max_life));
            float frame = lifetime_pos * (float)spritesheet_frames * config.sequence_multiplier;
            if (config.animation_mode == "once") {
                frame = fminf(frame, (float)(spritesheet_frames - 1));
            } else {
                frame = fmodf(frame, (float)spritesheet_frames);
            }
            particle.frame = frame;
        }
    }
    for (ParticleSystem* child : children) child->update(dt);
}
