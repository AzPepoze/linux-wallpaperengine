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
bool near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance * std::max(1.0f, std::fabs(b)); }

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
    ParticleEmitterConfig emitter;
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

float alphaAtLifeFraction(const char* json, float fraction) {
    cJSON* document = cJSON_Parse(json);
    ParticleSystemConfig config = ParticleParser::parse(document);
    cJSON_Delete(document);
    config.max_particles = 4;
    config.emitters.push_back(ParticleEmitterConfig{});
    config.initializers.push_back(scalarInitializer("lifetimerandom", 10.0f));
    ParticleSystem system(config, 3840.0f, 2160.0f);
    system.emitParticles(1);
    system.update(10.0f * fraction);
    return system.particles.empty() ? -1.0f : system.particles[0].alpha;
}

// Alpha fade: fade-in completes at `fadeintime` and fade-out starts at `fadeouttime` (fractions of the lifetime,
// 0.5 each when omitted), per the Wallpaper Engine operator documentation.
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
    ParticleEmitterConfig emitter;
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
    // The instantaneous burst fires with the instance; the local-space child of a worldspace parent is scaled by the
    // layer (x4) around the parent particle.
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
    // A follow instance's particles are cleared the moment their parent ends (reference behaviour), so nothing
    // hovers where the star died. Only the star itself fades, through its own alpha operator.
    CHECK(child->particles.empty());
}

void testPerInstanceCapacity() {
    ParticleSystem parent(makeConfig(0, 0.0f), 3840.0f, 2160.0f);
    ParticleSystemConfig config;
    config.max_particles = 2;  // capacity of each instance
    ParticleEmitterConfig emitter;
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
    config.emitters.push_back(ParticleEmitterConfig{});
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
    testAlphaFade();
    testWorldSpaceSpawn();
    testLocalSpaceUnchanged();
    testFollowChild();
    testFollowChildEndsWithParent();
    testPerInstanceCapacity();
    testEventDeathBurst();
    return test::finish("particle simulation tests");
}
