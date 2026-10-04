#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <string>
#include <vector>

#include "linmath.h"
#include "particle_parser.h"
#include "shared/graphics/gfx_resource.h"

struct Particle {
    vec3 position = {0, 0, 0};
    vec3 base_position = {0, 0, 0};
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
    float random_seed = 0.0f;
    float frame = -1.0f;

    float drag = 0.0f;
    vec3 gravity = {0, 0, 0};
    float fade_in = 0.0f;
    float fade_out = 0.0f;

    float osc_alpha_freq = 0.0f;
    float osc_alpha_min = 1.0f;
    float osc_size_freq = 0.0f;
    float osc_size_min = 1.0f;
    float osc_size_max = 1.0f;
    float osc_pos_freq = 0.0f;
    float osc_pos_min = 0.0f;
    float osc_pos_max = 0.0f;

    float turb_speed = 0.0f;
};

class EngineContext;
class ShaderPass;

// Wallpaper Engine child particle systems are spawned under one of these
// conditions (WE "Children" component).
enum class ParticleSpawnType {
    Static,       // one instance at the particle system origin
    EventFollow,  // created per parent particle and follows it
    EventSpawn,   // created when a parent particle spawns
    EventDeath,   // created where a parent particle dies
};

class ParticleSystem {
   public:
    std::string name;
    ParticleSystemConfig config;
    ShaderPass* material_pass = nullptr;
    std::vector<Particle> particles;
    std::vector<ParticleSystem*> children;

    int max_particles;
    std::vector<float> emitter_timers;
    float global_time = 0.0f;
    float scene_w, scene_h;
    vec3 layer_origin = {0, 0, 0};
    vec3 layer_scale = {1, 1, 1};
    float layer_rotation = 0.0f;
    vec2 parallax = {0, 0};
    bool is_additive = false;
    bool has_refract = false;
    bool is_trail = false;
    bool use_perspective = false;
    // A child system is emitted from its parent's particles rather than from its
    // own emitter region; this tracks its accumulated emission time.
    ParticleSpawnType spawn_type = ParticleSpawnType::Static;
    vec3 child_offset = {0, 0, 0};
    vec3 child_angles = {0, 0, 0};
    vec3 child_scale = {1, 1, 1};
    int child_maxcount = 20;
    float child_probability = 1.0f;
    float attached_emitter_timer = 0.0f;

    int spritesheet_cols = 0;
    int spritesheet_rows = 0;
    int spritesheet_frames = 0;
    float spritesheet_duration = 0.0f;
    int texture_width = 0;
    int texture_height = 0;

    float override_alpha = 1.0f;
    // Wallpaper Engine instance overrides. `rate` is a time scale for the whole system, `count` scales the
    // emission rate, and speed, lifetime and size scale each particle as it spawns.
    float override_rate = 1.0f;
    float override_size = 1.0f;
    float override_count = 1.0f;
    float override_speed = 1.0f;
    float override_lifetime = 1.0f;
    vec3 override_color = {1.0f, 1.0f, 1.0f};
    bool has_override_color = false;
    bool override_color_is_legacy = false;

    // Script playback control: `stop` ends emission and lets live particles finish, `pause` freezes the simulation.
    bool emitting = true;
    bool paused = false;
    // Control points are stored for scripts; nothing in the simulation reads them yet.
    float control_points[8][3] = {};
    void emitParticles(int count);
    void clearParticles();

    ParticleSystem(ParticleSystemConfig config, float sw, float h);
    ~ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    static ParticleSystem* createFromJSON(cJSON* node, EngineContext& ctx, float sw, float sh);
    static ParticleSystem* createFromPath(const char* particle_path, EngineContext& ctx, float sw, float sh,
                                          const ParticleObjectConfig& overrides = {});

    void update(float dt);
    // Emits this (child) system's particles from each parent particle's current
    // position, matching Wallpaper Engine's attached child-system behaviour.
    void emitFromParents(const ParticleSystem& parent, float dt);
    void draw(EngineContext& ctx);
    void drawDebugBounds(EngineContext& ctx);
    bool requiresSceneColor() const;
    void setSceneColorView(sg_view view);
    sg_view sceneColorView() const {
        return scene_color_view;
    }

    bool show_bounds = false;
    bool show_velocity = false;
    std::string config_path;
    std::string texture_path;

   private:
    GfxBuffer particle_vertex_buffer;
    GfxBuffer particle_index_buffer;
    sg_view scene_color_view = {SG_INVALID_ID};

    void spawnParticle(const float* parent_position = nullptr);
    void initParticleBuffers();
};

#endif  // PARTICLE_SYSTEM_H
