#include <math.h>
#include <stdlib.h>

#include <algorithm>

#include "particle_system.h"

ParticleSystem::ParticleSystem(ParticleSystemConfig config, float scene_width, float scene_height)
    : config(std::move(config)), scene_w(scene_width), scene_h(scene_height) {
    max_particles = std::max(0, this->config.max_particles);
    particles.reserve(max_particles);
    is_additive = this->config.additive;
    emitter_timers.resize(this->config.emitters.size(), 0.0f);
    is_rope_trail = this->config.renderer.type == "ropetrail";
    is_rope = is_rope_trail || this->config.renderer.type == "rope";
    for (int i = 0; i < 8; ++i) vec3_dup(control_points[i], this->config.control_points[i].offset);
}

size_t ParticleSystem::trailSegments() const {
    return (size_t)std::clamp(config.renderer.segments, 2, 64);
}

float ParticleSystem::trailSampleInterval() const {
    return (config.renderer.length > 0.0f ? config.renderer.length : 1.0f) / (float)trailSegments();
}

float ParticleSystem::trailSampleFraction() const {
    return std::clamp(trail_sample_timer / trailSampleInterval(), 0.0f, 1.0f);
}

void ParticleSystem::updateControlPoints(const float* cursor_local) {
    for (int i = 0; i < 8; ++i) {
        if ((config.control_points[i].flags & 1) && cursor_local)
            for (int axis = 0; axis < 3; ++axis)
                control_points[i][axis] = cursor_local[axis] + config.control_points[i].offset[axis];
    }
    for (ParticleSystem* child : children) child->updateControlPoints(cursor_local);
}

void ParticleSystem::emitParticles(int count) {
    for (int i = 0; i < count && i < 1000; ++i) spawnParticle();
}

void ParticleSystem::clearParticles() {
    particles.clear();
}

void ParticleSystem::update(float real_dt) {
    if (pending_warmup > 0.0f && (!simulatesInWorld() || has_placement)) {
        const float warmup = pending_warmup;
        pending_warmup = 0.0f;
        for (float time = 0.0f; time < warmup; time += 0.1f) step(0.1f);
    }
    step(real_dt);
}

void ParticleSystem::updateInstances(float dt) {
    if (instances.empty()) return;
    int burst = 0;
    float rate = 0.0f;
    for (const ParticleEmitterConfig& emitter : config.emitters) {
        burst += std::max(0, emitter.instantaneous);
        rate += emitter.rate;
    }
    rate *= override_count;

    for (ChildInstance& instance : instances) {
        if (!instance.alive) continue;
        const Particle* parent_particle = nullptr;
        if (parent_system) {
            for (const Particle& candidate : parent_system->particles) {
                if (candidate.serial == instance.parent_serial) {
                    parent_particle = &candidate;
                    break;
                }
            }
        }
        if (!parent_particle) {
            retireInstance(instance);
            continue;
        }
        if (instance.follow)
            for (int axis = 0; axis < 3; ++axis) instance.position[axis] = parent_particle->position[axis];
        if (!emitting) continue;

        // An instantaneous emitter fires again whenever its instance has nothing left alive.
        if (burst > 0) {
            bool empty = true;
            for (const Particle& particle : particles) {
                if (particle.source_instance == instance.id) {
                    empty = false;
                    break;
                }
            }
            if (empty)
                for (int index = 0; index < burst; ++index) spawnParticle(&instance);
        }
        if (rate > 0.0f) {
            instance.emit_timer += dt * rate;
            int emitted = 0;
            while (instance.emit_timer >= 1.0f && emitted++ < 64) {
                instance.emit_timer -= 1.0f;
                spawnParticle(&instance);
            }
            instance.emit_timer = std::min(instance.emit_timer, 1.0f);
        }
    }

    // Finished instances go away once none of their particles remain.
    instances.erase(std::remove_if(instances.begin(), instances.end(),
                                   [&](const ChildInstance& instance) {
                                       if (instance.alive) return false;
                                       for (const Particle& particle : particles)
                                           if (particle.source_instance == instance.id) return false;
                                       return true;
                                   }),
                    instances.end());
}

void ParticleSystem::step(float real_dt) {
    if (paused) return;
    // `rate` scales this system's own clock: emission, lifetimes and motion all run faster or slower.
    const float dt = real_dt * fmaxf(0.0f, override_rate);
    global_time += dt;
    updateInstances(dt);
    if (spawn_type == ParticleSpawnType::Static && emitting) {
        for (size_t emitter_index = 0; emitter_index < config.emitters.size(); ++emitter_index) {
            const float rate = config.emitters[emitter_index].rate * override_count;
            if (rate > 0) {
                emitter_timers[emitter_index] += dt;
                const float interval = 1.0f / rate;
                while (emitter_timers[emitter_index] >= interval) {
                    spawnParticle();
                    emitter_timers[emitter_index] -= interval;
                }
            }
        }
    }
    bool sample_trail_history = false;
    if (is_rope_trail) {
        trail_sample_timer += dt;
        const float sample_interval = trailSampleInterval();
        if (trail_sample_timer >= sample_interval) {
            trail_sample_timer = fmodf(trail_sample_timer, sample_interval);
            sample_trail_history = true;
        }
    }
    for (size_t index = 0; index < particles.size(); ++index) {
        Particle& particle = particles[index];
        particle.life -= dt;
        if (particle.life <= 0) {
            // Event-death children are created where this particle died; follow and spawn instances end with it.
            for (ParticleSystem* child : children) {
                if (child->spawn_type == ParticleSpawnType::EventDeath) child->createInstance(particle, false);
                else child->endInstances(particle.serial);
            }
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
        // Follow particles are offsets from their instance, which tracks the parent particle.
        const ChildInstance* anchor = particle.instance_id != 0 ? findInstance(particle.instance_id) : nullptr;
        particle.position[0] = particle.base_position[0] + (anchor ? anchor->position[0] : 0.0f);
        particle.position[1] = particle.base_position[1] + (anchor ? anchor->position[1] : 0.0f);
        if (anchor) particle.position[2] = particle.base_position[2] + anchor->position[2];
        if (particle.osc_pos_freq > 0) {
            const float wave = sinf(global_time * particle.osc_pos_freq + particle.random_seed * 100.0f);
            const float amplitude = particle.osc_pos_min + (particle.osc_pos_max - particle.osc_pos_min) * 0.5f;
            particle.position[0] += wave * amplitude;
            particle.position[1] += cosf(global_time * particle.osc_pos_freq) * amplitude;
        }
        if (sample_trail_history) {
            particle.history.push_back({particle.position[0], particle.position[1], particle.position[2]});
            if (particle.history.size() > trailSegments()) particle.history.erase(particle.history.begin());
        }
        const float age = particle.max_life - particle.life;
        float alpha = particle.initial_alpha * override_alpha;
        const float life_norm = particle.max_life > 0.0f ? age / particle.max_life : 0.0f;
        // Alpha fade operator: ramp up until fade_in, then down from fade_out to the end of the life.
        if (particle.fade_in > 0.0f && life_norm <= particle.fade_in) {
            alpha *= life_norm / particle.fade_in;
        } else if (particle.fade_out < 1.0f && life_norm > particle.fade_out) {
            alpha *= 1.0f - (life_norm - particle.fade_out) / (1.0f - particle.fade_out);
        }
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
    for (ParticleSystem* child : children) child->update(real_dt);
}
