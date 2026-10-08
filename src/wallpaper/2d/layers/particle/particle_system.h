#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <array>
#include <string>
#include <vector>

#include "linmath.h"
#include "particle_parser.h"
#include "shared/graphics/gfx_resource.h"

struct Particle {
    uint64_t serial = 0;
    std::vector<std::array<float, 3>> history;
    // Following event-child particles store base_position as an offset from their instance.
    uint64_t instance_id = 0;
    uint64_t source_instance = 0;
    vec3 position = {0, 0, 0};
    vec3 base_position = {0, 0, 0};
    vec3 velocity = {0, 0, 0};
    vec3 color = {1, 1, 1};
    vec3 initial_color = {1, 1, 1};
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
    // Lifetime fractions; the defaults mean no alphafade operator.
    float fade_in = 0.0f;
    float fade_out = 1.0f;

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

enum class ParticleSpawnType {
    Static,
    EventFollow,
    EventSpawn,
    EventDeath,
};

// Y-up scene coordinates, without camera parallax.
struct ParticlePlacement {
    float origin[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
    float rotation_deg = 0.0f;
};

// One emitting instance of an event child system.
struct ChildInstance {
    uint64_t id = 0;
    uint64_t parent_serial = 0;
    vec3 position = {0, 0, 0};
    bool follow = false;
    bool alive = true;
    float emit_timer = 0.0f;
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
    bool is_rope = false;
    bool is_rope_trail = false;
    uint64_t next_serial = 0;
    size_t trailSegments() const;
    // Trail history is sampled by the whole system at once so one shader value can blend between samples.
    float trailSampleInterval() const;
    float trailSampleFraction() const;
    float trail_sample_timer = 0.0f;
    bool use_perspective = false;
    ParticleSpawnType spawn_type = ParticleSpawnType::Static;
    vec3 child_offset = {0, 0, 0};
    vec3 child_angles = {0, 0, 0};
    vec3 child_scale = {1, 1, 1};
    int child_maxcount = 20;
    float child_probability = 1.0f;
    // Particle flag bit 1: particles ignore the system's transform after they spawn.
    bool world_space = false;
    ParticlePlacement placement;
    bool has_placement = false;
    float camera_offset[2] = {0.0f, 0.0f};
    ParticleSystem* parent_system = nullptr;
    std::vector<ChildInstance> instances;
    uint64_t next_instance_id = 1;
    // Start-time warm-up runs on the first update, once the placement is known.
    float pending_warmup = 0.0f;

    int spritesheet_cols = 0;
    int spritesheet_rows = 0;
    int spritesheet_frames = 0;
    float spritesheet_duration = 0.0f;
    int texture_width = 0;
    int texture_height = 0;

    float override_alpha = 1.0f;
    // Instance overrides: rate scales the system clock, count the emission, the rest each particle.
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
    // Control points are in the particle system's local coordinate space.
    float control_points[8][3] = {};
    void updateControlPoints(const float* cursor_local);
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
    // Worldspace systems convert spawn values with this placement.
    void setPlacement(const ParticlePlacement& value, float camera_offset_x, float camera_offset_y);
    bool simulatesInWorld() const;
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

    void step(float dt);
    void spawnParticle(const ChildInstance* instance = nullptr);
    void createInstance(const Particle& parent_particle, bool follow);
    void endInstances(uint64_t parent_serial);
    // Stops an instance's emission; a follow child's particles end with their parent particle.
    void retireInstance(ChildInstance& instance);
    void updateInstances(float dt);
    ChildInstance* findInstance(uint64_t id);
    void placementMatrix(float matrix[4]) const;
    void toWorldPosition(const float* local, float* world) const;
    float placementSizeScale() const;
    void initParticleBuffers();
};

#endif  // PARTICLE_SYSTEM_H
