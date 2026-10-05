#include <math.h>
#include <stdlib.h>

#include <algorithm>

#include "particle_system.h"

namespace {
float randomFloat() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

// Initializer draws are pow(random, exponent) between the bounds; an exponent of 1 is a plain uniform draw.
float randomFloat(float exponent) {
    const float value = randomFloat();
    return exponent == 1.0f ? value : powf(value, exponent);
}
}  // namespace

void ParticleSystem::placementMatrix(float matrix[4]) const {
    // Column-major R(rotation) * diag(scale): world = origin + M * local.
    const float radians = placement.rotation_deg * (float)(M_PI / 180.0);
    const float c = cosf(radians), s = sinf(radians);
    matrix[0] = c * placement.scale[0];
    matrix[1] = s * placement.scale[0];
    matrix[2] = -s * placement.scale[1];
    matrix[3] = c * placement.scale[1];
}

void ParticleSystem::toWorldPosition(const float* local, float* world) const {
    float matrix[4];
    placementMatrix(matrix);
    world[0] = placement.origin[0] + matrix[0] * local[0] + matrix[2] * local[1];
    world[1] = placement.origin[1] + matrix[1] * local[0] + matrix[3] * local[1];
    world[2] = placement.origin[2] + local[2] * placement.scale[2];
}

float ParticleSystem::placementSizeScale() const {
    return fabsf(placement.scale[0]);
}

void ParticleSystem::setPlacement(const ParticlePlacement& value, float camera_offset_x, float camera_offset_y) {
    placement = value;
    has_placement = true;
    camera_offset[0] = camera_offset_x;
    camera_offset[1] = camera_offset_y;
    for (ParticleSystem* child : children) child->setPlacement(value, camera_offset_x, camera_offset_y);
}

bool ParticleSystem::simulatesInWorld() const {
    return world_space || (parent_system && parent_system->simulatesInWorld());
}

ChildInstance* ParticleSystem::findInstance(uint64_t id) {
    for (ChildInstance& instance : instances)
        if (instance.id == id) return &instance;
    return nullptr;
}

void ParticleSystem::createInstance(const Particle& parent_particle, bool follow) {
    if (child_probability < 1.0f && randomFloat() > child_probability) return;
    if (child_maxcount > 0) {
        int alive = 0;
        for (const ChildInstance& existing : instances) alive += existing.alive ? 1 : 0;
        if (alive >= child_maxcount) return;
    }
    ChildInstance instance;
    instance.id = next_instance_id++;
    instance.parent_serial = parent_particle.serial;
    for (int axis = 0; axis < 3; ++axis) instance.position[axis] = parent_particle.position[axis];
    instance.follow = follow;
    instance.alive = spawn_type != ParticleSpawnType::EventDeath;
    instances.push_back(instance);

    int burst = 0;
    for (const ParticleEmitterConfig& emitter : config.emitters) burst += std::max(0, emitter.instantaneous);
    // An event child without an instantaneous count still marks the event with one particle.
    if (burst == 0 && spawn_type == ParticleSpawnType::EventDeath) burst = 1;
    const ChildInstance created = instances.back();
    for (int index = 0; index < burst; ++index) spawnParticle(&created);
}

void ParticleSystem::retireInstance(ChildInstance& instance) {
    if (!instance.alive) return;
    instance.alive = false;
    // Wallpaper Engine clears a follow instance's particles as soon as its parent particle ends (no fade); spawn
    // and death children keep theirs until they expire.
    if (spawn_type != ParticleSpawnType::EventFollow) return;
    for (Particle& particle : particles)
        if (particle.source_instance == instance.id) particle.life = 0.0f;
}

void ParticleSystem::endInstances(uint64_t parent_serial) {
    for (ChildInstance& instance : instances)
        if (instance.parent_serial == parent_serial) retireInstance(instance);
}

void ParticleSystem::spawnParticle(const ChildInstance* instance) {
    if (static_cast<int>(particles.size()) >= max_particles) return;
    if (instance) {
        // Every instance has its own particle capacity (the particle file's max count).
        int owned = 0;
        for (const Particle& existing : particles) owned += existing.source_instance == instance->id ? 1 : 0;
        if (owned >= std::max(1, config.max_particles)) return;
    }
    Particle particle = {};
    particle.serial = next_serial++;
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
        if (!instance) {
            const int point = emitter.control_point;
            if (point >= 0 && point < 8)
                for (int axis = 0; axis < 3; ++axis) particle.position[axis] += control_points[point][axis];
        }
    }
    // Child instance offset (WE "Children" component).
    particle.position[0] += child_offset[0];
    particle.position[1] += child_offset[1];
    particle.position[2] += child_offset[2];
    for (const ParticleInitializerConfig& initializer : config.initializers) {
        if (initializer.type == "lifetimerandom") {
            particle.max_life = initializer.minimum_scalar +
                                randomFloat(initializer.exponent) * (initializer.maximum_scalar - initializer.minimum_scalar);
            particle.life = particle.max_life;
        } else if (initializer.type == "sizerandom") {
            particle.size = initializer.minimum_scalar +
                            randomFloat(initializer.exponent) * (initializer.maximum_scalar - initializer.minimum_scalar);
            particle.initial_size = particle.size;
        } else if (initializer.type == "velocityrandom") {
            particle.velocity[0] =
                initializer.minimum[0] + randomFloat(initializer.exponent) * (initializer.maximum[0] - initializer.minimum[0]);
            particle.velocity[1] =
                initializer.minimum[1] + randomFloat(initializer.exponent) * (initializer.maximum[1] - initializer.minimum[1]);
        } else if (initializer.type == "colorrandom") {
            // One blend factor for all channels: independent draws would tint ranges such as white..grey.
            const float blend = randomFloat();
            for (int component = 0; component < 3; ++component)
                particle.color[component] =
                    (initializer.minimum[component] +
                     blend * (initializer.maximum[component] - initializer.minimum[component])) /
                    255.0f;
        } else if (initializer.type == "alpharandom") {
            particle.alpha = initializer.minimum_scalar +
                             randomFloat(initializer.exponent) * (initializer.maximum_scalar - initializer.minimum_scalar);
            particle.initial_alpha = particle.alpha;
        } else if (initializer.type == "rotationrandom") {
            particle.rotation =
                initializer.minimum[2] + randomFloat() * (initializer.maximum[2] - initializer.minimum[2]);
        } else if (initializer.type == "angularvelocityrandom") {
            particle.angular_vel =
                initializer.minimum[2] + randomFloat() * (initializer.maximum[2] - initializer.minimum[2]);
        } else if (initializer.type == "turbulentvelocityrandom") {
            // WE seeds this with curl noise; keep the authored forward direction, phase and
            // speed so the trail has a valid direction before turbulence evolves it.
            const float phase = initializer.turbulence_offset + (randomFloat() - 0.5f) * initializer.turbulence_scale;
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
    vec3_dup(particle.initial_color, particle.color);
    particle.max_life *= override_lifetime;
    particle.life = particle.max_life;
    particle.velocity[0] *= override_speed;
    particle.velocity[1] *= override_speed;
    particle.angular_vel *= override_speed;
    particle.size *= override_size;
    particle.initial_size *= override_size;
    particle.size *= child_scale[0];
    particle.initial_size *= child_scale[0];

    // Positions are authored in the system's local space. Worldspace systems leave it here: position and velocity
    // take the layer's origin, rotation and scale once, and everything after that happens in scene coordinates.
    const bool parent_in_world = instance && parent_system && parent_system->simulatesInWorld();
    float matrix[4];
    placementMatrix(matrix);
    auto transformVelocity = [&]() {
        const float x = particle.velocity[0], y = particle.velocity[1];
        particle.velocity[0] = matrix[0] * x + matrix[2] * y;
        particle.velocity[1] = matrix[1] * x + matrix[3] * y;
    };
    bool offset_from_instance = false;
    if (instance) {
        particle.source_instance = instance->id;
        if (world_space) {
            float anchor[3] = {instance->position[0], instance->position[1], instance->position[2]};
            if (!parent_in_world && parent_system) parent_system->toWorldPosition(instance->position, anchor);
            const float x = particle.position[0], y = particle.position[1];
            particle.position[0] = anchor[0] + matrix[0] * x + matrix[2] * y;
            particle.position[1] = anchor[1] + matrix[1] * x + matrix[3] * y;
            particle.position[2] = anchor[2] + particle.position[2] * placement.scale[2];
            transformVelocity();
        } else if (parent_in_world) {
            // A local-space child of a worldspace parent is drawn with the layer transform around its anchor.
            const float x = particle.position[0], y = particle.position[1];
            particle.position[0] = matrix[0] * x + matrix[2] * y;
            particle.position[1] = matrix[1] * x + matrix[3] * y;
            transformVelocity();
            particle.size *= placementSizeScale();
            particle.initial_size *= placementSizeScale();
            offset_from_instance = instance->follow;
            if (!offset_from_instance)
                for (int axis = 0; axis < 3; ++axis) particle.position[axis] += instance->position[axis];
        } else {
            offset_from_instance = instance->follow;
            if (!offset_from_instance)
                for (int axis = 0; axis < 3; ++axis) particle.position[axis] += instance->position[axis];
        }
        if (offset_from_instance) particle.instance_id = instance->id;
    } else if (world_space) {
        float world[3];
        toWorldPosition(particle.position, world);
        for (int axis = 0; axis < 3; ++axis) particle.position[axis] = world[axis];
        transformVelocity();
    }
    if (offset_from_instance)
        for (int axis = 0; axis < 3; ++axis) particle.position[axis] += instance->position[axis];
    particle.base_position[0] = offset_from_instance ? particle.position[0] - instance->position[0] : particle.position[0];
    particle.base_position[1] = offset_from_instance ? particle.position[1] - instance->position[1] : particle.position[1];
    particle.base_position[2] = offset_from_instance ? particle.position[2] - instance->position[2] : particle.position[2];
    if (is_rope_trail) particle.history.push_back({particle.position[0], particle.position[1], particle.position[2]});
    for (const ParticleOperatorConfig& particle_operator : config.operators) {
        if (particle_operator.type == "movement") {
            vec3_dup(particle.gravity, particle_operator.gravity);
            if (parent_in_world && !world_space) {
                // Gravity authored for a local-space child turns with the layer.
                particle.gravity[0] = matrix[0] * particle_operator.gravity[0] + matrix[2] * particle_operator.gravity[1];
                particle.gravity[1] = matrix[1] * particle_operator.gravity[0] + matrix[3] * particle_operator.gravity[1];
            }
            particle.drag = particle_operator.drag;
        } else if (particle_operator.type == "alphafade") {
            particle.fade_in = particle_operator.fade_in_time;
            particle.fade_out = particle_operator.fade_out_time;
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

    // Event children get one instance per parent particle: spawn children stay where the particle was created,
    // follow children track it until it dies.
    for (ParticleSystem* child : children) {
        if (child->spawn_type == ParticleSpawnType::EventSpawn) child->createInstance(particle, false);
        else if (child->spawn_type == ParticleSpawnType::EventFollow) child->createInstance(particle, true);
    }
}
