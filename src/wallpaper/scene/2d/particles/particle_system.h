#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <string>
#include <vector>

#include "core/gfx_resource.h"
#include "linmath.h"
#include "sokol_gfx.h"
#include "wallpaper/scene/2d/parser/particle_parser.h"

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

    int spritesheet_cols = 0;
    int spritesheet_rows = 0;
    int spritesheet_frames = 0;
    float spritesheet_duration = 0.0f;
    int texture_width = 0;
    int texture_height = 0;

    float override_alpha = 1.0f;
    float override_rate = 1.0f;
    vec3 override_color = {1.0f, 1.0f, 1.0f};
    bool has_override_color = false;
    bool override_color_is_legacy = false;

    ParticleSystem(ParticleSystemConfig config, float sw, float h);
    ~ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    static ParticleSystem* createFromJSON(cJSON* node, EngineContext& ctx, float sw, float sh);
    static ParticleSystem* createFromPath(const char* particle_path, EngineContext& ctx, float sw, float sh,
                                          float override_alpha = 1.0f, float override_rate = 1.0f,
                                          const float* override_color = nullptr, bool override_color_is_legacy = false);

    void update(float dt);
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

    void spawnParticle();
    void initParticleBuffers();
};

#endif  // PARTICLE_SYSTEM_H
