#include <cmath>

#include "test_util.h"
#include "wallpaper/2d/layers/particle/particle_parser.h"
#include "wallpaper/2d/layers/particle/particle_system.h"

// The simulation tests never create GPU resources; the RAII wrappers only reference these on destruction.
void sg_destroy_buffer(sg_buffer) {}
void sg_destroy_image(sg_image) {}
void sg_destroy_view(sg_view) {}
void sg_destroy_pipeline(sg_pipeline) {}
void sg_destroy_shader(sg_shader) {}

ParticleSystem::~ParticleSystem() {
    for (ParticleSystem* child : children) delete child;
}

namespace {
bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance * std::max(1.0f, std::fabs(b));
}

// Emitters must not emit on their own; the struct default is WE's rate of 5.
ParticleEmitterConfig manualEmitter() {
    ParticleEmitterConfig emitter;
    emitter.rate = 0.0f;
    emitter.distance_max[0] = emitter.distance_max[1] = emitter.distance_max[2] = 0.0f;  // spawn exactly at origin
    return emitter;
}

ParticleInitializerConfig scalarInitializer(const char* type, float value) {
    ParticleInitializerConfig initializer;
    initializer.type = type;
    initializer.minimum_scalar = initializer.maximum_scalar = value;
    return initializer;
}

ParticleInitializerConfig velocityInitializer(float x, float y) {
    ParticleInitializerConfig initializer;
    initializer.type = "velocityrandom";
    initializer.minimum[0] = initializer.maximum[0] = x;
    initializer.minimum[1] = initializer.maximum[1] = y;
    return initializer;
}

ParticleSystemConfig makeConfig(int flags, float gravity_y = -40.0f) {
    ParticleSystemConfig config;
    config.flags = flags;
    config.max_particles = 16;
    ParticleEmitterConfig emitter = manualEmitter();
    emitter.type = "sphererandom";  // zero radius: always the emitter origin
    config.emitters.push_back(emitter);
    config.initializers.push_back(scalarInitializer("lifetimerandom", 4.0f));
    config.initializers.push_back(scalarInitializer("sizerandom", 10.0f));
    config.initializers.push_back(velocityInitializer(100.0f, 0.0f));
    ParticleOperatorConfig movement;
    movement.type = "movement";
    movement.gravity[1] = gravity_y;
    config.operators.push_back(movement);
    return config;
}

ParticlePlacement mirroredRotatedPlacement() {
    ParticlePlacement placement;
    placement.origin[0] = 1000.0f;
    placement.origin[1] = 500.0f;
    placement.scale[0] = -4.0f;
    placement.scale[1] = 4.0f;
    placement.rotation_deg = 15.0f;
    return placement;
}

void testParsing() {
    cJSON* json = cJSON_Parse(R"({"flags":1,"emitter":[{"name":"sphererandom","rate":0,"instantaneous":3}]})");
    const ParticleSystemConfig config = ParticleParser::parse(json);
    cJSON_Delete(json);
    CHECK(config.flags == 1);
    CHECK(config.emitters.size() == 1 && config.emitters[0].instantaneous == 3);
}

// Keys a preset leaves out take Wallpaper Engine's defaults rather than zero.
void testDefaultsForOmittedKeys() {
    cJSON* json = cJSON_Parse(R"({
        "emitter":[{"name":"boxrandom"}],
        "initializer":[{"name":"alpharandom"},{"name":"sizerandom"},{"name":"lifetimerandom"},
                       {"name":"velocityrandom"},{"name":"rotationrandom"},{"name":"angularvelocityrandom"},
                       {"name":"alpharandom","min":0.1,"max":0.2,"exponent":2}]})");
    const ParticleSystemConfig config = ParticleParser::parse(json);
    cJSON_Delete(json);
    CHECK(config.emitters.size() == 1);
    if (config.emitters.empty() || config.initializers.size() != 7) return;
    CHECK(near(config.emitters[0].rate, 5.0f));
    CHECK(near(config.emitters[0].distance_max[0], 256.0f) && near(config.emitters[0].distance_max[1], 256.0f));
    CHECK(near(config.initializers[0].minimum_scalar, 0.05f) && near(config.initializers[0].maximum_scalar, 1.0f));
    CHECK(near(config.initializers[1].maximum_scalar, 20.0f));
    CHECK(near(config.initializers[2].maximum_scalar, 1.0f));
    CHECK(near(config.initializers[3].minimum[0], -32.0f) && near(config.initializers[3].maximum[1], 32.0f));
    CHECK(near(config.initializers[4].maximum[2], 2.0f * (float)M_PI));
    CHECK(near(config.initializers[5].minimum[2], -5.0f) && near(config.initializers[5].maximum[2], 5.0f));
    CHECK(near(config.initializers[6].minimum_scalar, 0.1f) && near(config.initializers[6].maximum_scalar, 0.2f));
    CHECK(near(config.initializers[6].exponent, 2.0f));
}

float alphaAtLifeFraction(const char* json, float fraction) {
    cJSON* document = cJSON_Parse(json);
    ParticleSystemConfig config = ParticleParser::parse(document);
    cJSON_Delete(document);
    config.max_particles = 4;
    config.emitters.push_back(manualEmitter());
    config.initializers.push_back(scalarInitializer("lifetimerandom", 10.0f));
    ParticleSystem system(config, 3840.0f, 2160.0f);
    system.emitParticles(1);
    system.update(10.0f * fraction);
    return system.particles.empty() ? -1.0f : system.particles[0].alpha;
}

// Fade-in and fade-out are lifetime fractions, 0.5 each when omitted, per WE's operator docs.
void testAlphaFade() {
    const char* defaults = R"({"operator":[{"name":"alphafade"}]})";
    CHECK(near(alphaAtLifeFraction(defaults, 0.25f), 0.5f));
    CHECK(near(alphaAtLifeFraction(defaults, 0.5f), 1.0f));
    CHECK(near(alphaAtLifeFraction(defaults, 0.75f), 0.5f));

    const char* custom = R"({"operator":[{"name":"alphafade","fadeintime":0.2,"fadeouttime":0.8}]})";
    CHECK(near(alphaAtLifeFraction(custom, 0.1f), 0.5f));
    CHECK(near(alphaAtLifeFraction(custom, 0.5f), 1.0f));
    CHECK(near(alphaAtLifeFraction(custom, 0.9f), 0.5f));

    // A fade-out time of 1 means the fade-out never starts.
    const char* hold = R"({"operator":[{"name":"alphafade","fadeintime":0.1,"fadeouttime":1}]})";
    CHECK(near(alphaAtLifeFraction(hold, 0.95f), 1.0f));

    // No alphafade operator: alpha is untouched.
    CHECK(near(alphaAtLifeFraction(R"({"operator":[]})", 0.05f), 1.0f));
    CHECK(near(alphaAtLifeFraction(R"({"operator":[]})", 0.95f), 1.0f));
}

// One particle (10 s life, size 10, at (x, 0)) advanced to a fraction of its life; small steps keep error negligible.
Particle particleAt(const char* json, float fraction, float x = 0.0f, float step = 0.05f) {
    cJSON* document = cJSON_Parse(json);
    ParticleSystemConfig config = ParticleParser::parse(document);
    cJSON_Delete(document);
    config.max_particles = 4;
    ParticleEmitterConfig emitter = manualEmitter();
    emitter.origin[0] = x;
    config.emitters.push_back(emitter);
    config.initializers.push_back(scalarInitializer("lifetimerandom", 10.0f));
    config.initializers.push_back(scalarInitializer("sizerandom", 10.0f));
    ParticleSystem system(config, 3840.0f, 2160.0f);
    system.emitParticles(1);
    const int steps = (int)std::lround(10.0f * fraction / step);
    for (int index = 0; index < steps; ++index) system.update(step);
    return system.particles.empty() ? Particle{} : system.particles[0];
}

void testSizeAndColorChange() {
    // Defaults: start 1, end 0, from 0 to 1 of the life; here the ramp only starts at half.
    const char* shrink = R"({"operator":[{"name":"sizechange","starttime":0.5}]})";
    CHECK(near(particleAt(shrink, 0.25f).size, 10.0f));
    CHECK(near(particleAt(shrink, 0.75f).size, 5.0f));
    CHECK(near(particleAt(shrink, 0.95f).size, 1.0f, 0.2f));

    const char* grow = R"({"operator":[{"name":"sizechange","startvalue":0,"endvalue":2}]})";
    CHECK(near(particleAt(grow, 0.5f).size, 10.0f));

    // Color: per channel multiplier from "1 1 1" to the end value.
    const char* tint = R"({"operator":[{"name":"colorchange","endvalue":"0.5 0.25 1"}]})";
    const Particle half = particleAt(tint, 0.5f);
    CHECK(near(half.color[0], 0.75f) && near(half.color[1], 0.625f) && near(half.color[2], 1.0f));
    const Particle fade_to_black = particleAt(R"({"operator":[{"name":"colorchange"}]})", 0.5f);
    CHECK(near(fade_to_black.color[0], 0.5f));

    const char* alpha_change = R"({"operator":[{"name":"alphachange","starttime":0,"endtime":1}]})";
    CHECK(near(particleAt(alpha_change, 0.5f).alpha, 0.5f));
}

void testAngularMovementAndAttract() {
    // Angular acceleration of -1 rad/s^2 for 2 s: angular velocity -2, rotation about -2.
    const Particle spinning = particleAt(R"({"operator":[{"name":"angularmovement","force":"0 0 -1"}]})", 0.2f);
    CHECK(near(spinning.angular_vel, -2.0f));
    CHECK(near(spinning.rotation, -2.05f, 0.05f));

    // Control point attract: a negative scale pushes a particle inside the threshold away from the control point.
    const char* repel =
        R"({"operator":[{"name":"controlpointattract","controlpoint":1,"scale":-1024,"threshold":32,"origin":"0 0 0"}]})";
    const Particle pushed = particleAt(repel, 0.005f, 10.0f, 0.05f);
    // impulse = scale * dt * (1 - distance / threshold) = 1024 * 0.05 * (1 - 10 / 32), applied along the line away.
    CHECK(near(pushed.velocity[0], 35.2f, 0.02f));
    // Outside the threshold nothing happens.
    const Particle untouched = particleAt(repel, 0.005f, 40.0f, 0.05f);
    CHECK(near(untouched.velocity[0], 0.0f));
    // A positive scale pulls it in.
    const char* pull = R"({"operator":[{"name":"controlpointattract","controlpoint":1,"scale":512,"threshold":64}]})";
    CHECK(particleAt(pull, 0.005f, 20.0f, 0.05f).velocity[0] < -10.0f);
}

void testWorldSpaceSpawn() {
    ParticleSystem system(makeConfig(1), 3840.0f, 2160.0f);
    system.world_space = true;
    system.setPlacement(mirroredRotatedPlacement(), 0.0f, 0.0f);
    CHECK(system.simulatesInWorld());
    system.emitParticles(1);
    CHECK(system.particles.size() == 1);
    if (system.particles.empty()) return;

    const Particle& particle = system.particles[0];
    const float radians = 15.0f * (float)(M_PI / 180.0);
    // The spawn point is the layer origin; velocity takes the mirrored, rotated and x4-scaled layer axes.
    CHECK(near(particle.position[0], 1000.0f) && near(particle.position[1], 500.0f));
    CHECK(near(particle.velocity[0], -400.0f * cosf(radians)));
    CHECK(near(particle.velocity[1], -400.0f * sinf(radians)));
    // Sizes are not scaled by the layer in worldspace.
    CHECK(near(particle.size, 10.0f));

    // Gravity is a world vector: one second later vy has dropped by exactly 40, not by the layer-scaled value.
    const float start_vy = particle.velocity[1];
    for (int step = 0; step < 10; ++step) system.update(0.1f);
    CHECK(system.particles.size() == 1);
    CHECK(near(system.particles[0].velocity[1], start_vy - 40.0f, 0.02f));
    CHECK(near(system.particles[0].size, 10.0f));
}

void testLocalSpaceUnchanged() {
    ParticleSystem system(makeConfig(0), 3840.0f, 2160.0f);
    system.setPlacement(mirroredRotatedPlacement(), 0.0f, 0.0f);
    CHECK(!system.simulatesInWorld());
    system.emitParticles(1);
    CHECK(system.particles.size() == 1);
    if (system.particles.empty()) return;
    // Local-space systems keep authored local values; the draw transform does the rest.
    CHECK(near(system.particles[0].position[0], 0.0f) && near(system.particles[0].position[1], 0.0f));
    CHECK(near(system.particles[0].velocity[0], 100.0f) && near(system.particles[0].velocity[1], 0.0f));
    system.update(1.0f);
    CHECK(near(system.particles[0].velocity[1], -40.0f, 0.02f));
}

ParticleSystem* makeFollowChild(ParticleSystem& parent, float lifetime) {
    ParticleSystemConfig config;
    config.max_particles = 8;
    ParticleEmitterConfig emitter = manualEmitter();
    emitter.type = "sphererandom";
    emitter.instantaneous = 1;
    config.emitters.push_back(emitter);
    config.initializers.push_back(scalarInitializer("lifetimerandom", lifetime));
    config.initializers.push_back(scalarInitializer("sizerandom", 100.0f));
    auto* child = new ParticleSystem(config, 3840.0f, 2160.0f);
    child->spawn_type = ParticleSpawnType::EventFollow;
    child->parent_system = &parent;
    child->child_maxcount = 100;
    parent.children.push_back(child);
    return child;
}

void testFollowChild() {
    ParticleSystem parent(makeConfig(1, 0.0f), 3840.0f, 2160.0f);
    parent.world_space = true;
    ParticleSystem* child = makeFollowChild(parent, 0.5f);
    parent.setPlacement(mirroredRotatedPlacement(), 0.0f, 0.0f);

    parent.emitParticles(1);
    CHECK(parent.particles.size() == 1);
    // A local-space child of a worldspace parent is scaled by the layer (x4) around the parent.
    CHECK(child->particles.size() == 1);
    if (parent.particles.empty() || child->particles.empty()) return;
    CHECK(near(child->particles[0].size, 400.0f));
    CHECK(child->simulatesInWorld());

    // It follows the parent every frame.
    for (int step = 0; step < 3; ++step) parent.update(0.1f);
    CHECK(child->particles.size() == 1);
    if (child->particles.empty() || parent.particles.empty()) return;
    CHECK(near(child->particles[0].position[0], parent.particles[0].position[0]));
    CHECK(near(child->particles[0].position[1], parent.particles[0].position[1]));
    CHECK(parent.particles[0].position[0] < 990.0f);  // it moved, so following is not just "same spawn point"

    // The instance fires again once its particle has expired while the parent is still alive.
    for (int step = 0; step < 10; ++step) parent.update(0.1f);
    CHECK(parent.particles.size() == 1);
    CHECK(child->particles.size() >= 1);

    // When the parent dies its instance ends and the child's last particle simply runs out.
    parent.particles[0].life = 0.01f;
    parent.update(0.1f);
    CHECK(parent.particles.empty());
    for (int step = 0; step < 20; ++step) parent.update(0.1f);
    CHECK(child->particles.empty());
    CHECK(child->instances.empty());
}

void testFollowChildEndsWithParent() {
    ParticleSystem parent(makeConfig(1, 0.0f), 3840.0f, 2160.0f);
    parent.world_space = true;
    ParticleSystem* child = makeFollowChild(parent, 5.0f);  // far outlives the parent
    parent.setPlacement(mirroredRotatedPlacement(), 0.0f, 0.0f);
    parent.emitParticles(1);
    CHECK(child->particles.size() == 1);
    if (parent.particles.empty() || child->particles.empty()) return;
    parent.particles[0].life = 0.01f;
    parent.update(0.1f);
    CHECK(parent.particles.empty());
    // A follow instance's particles clear when the parent ends; only the star itself fades.
    CHECK(child->particles.empty());
}

void testPerInstanceCapacity() {
    ParticleSystem parent(makeConfig(0, 0.0f), 3840.0f, 2160.0f);
    ParticleSystemConfig config;
    config.max_particles = 2;  // capacity of each instance
    ParticleEmitterConfig emitter = manualEmitter();
    emitter.type = "sphererandom";
    emitter.instantaneous = 5;  // asks for more than one instance may hold
    config.emitters.push_back(emitter);
    config.initializers.push_back(scalarInitializer("lifetimerandom", 4.0f));
    auto* child = new ParticleSystem(config, 3840.0f, 2160.0f);
    child->spawn_type = ParticleSpawnType::EventFollow;
    child->parent_system = &parent;
    child->child_maxcount = 10;
    child->max_particles = 2 * 10;  // room for every instance, as createFromPath arranges
    parent.children.push_back(child);

    parent.emitParticles(3);
    CHECK(parent.particles.size() == 3);
    // Three stars, three instances, two particles each; not "two particles for the whole system".
    CHECK(child->particles.size() == 6);
    CHECK(child->instances.size() == 3);
}

void testEventDeathBurst() {
    ParticleSystem parent(makeConfig(0, 0.0f), 3840.0f, 2160.0f);
    ParticleSystemConfig config;
    config.max_particles = 8;
    config.emitters.push_back(manualEmitter());
    config.initializers.push_back(scalarInitializer("lifetimerandom", 1.0f));
    auto* child = new ParticleSystem(config, 3840.0f, 2160.0f);
    child->spawn_type = ParticleSpawnType::EventDeath;
    child->parent_system = &parent;
    parent.children.push_back(child);

    parent.emitParticles(1);
    CHECK(child->particles.empty());
    parent.particles[0].life = 0.01f;
    parent.update(0.1f);
    CHECK(parent.particles.empty());
    CHECK(child->particles.size() == 1);  // one marker particle where the parent died
}
}  // namespace

int main() {
    testParsing();
    testDefaultsForOmittedKeys();
    testAlphaFade();
    testSizeAndColorChange();
    testAngularMovementAndAttract();
    testWorldSpaceSpawn();
    testLocalSpaceUnchanged();
    testFollowChild();
    testFollowChildEndsWithParent();
    testPerInstanceCapacity();
    testEventDeathBurst();
    return test::finish("particle simulation tests");
}
