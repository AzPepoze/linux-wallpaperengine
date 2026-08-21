#ifndef WALLPAPER_ENGINE_SCENE_DOCUMENT_H
#define WALLPAPER_ENGINE_SCENE_DOCUMENT_H

#include <stdint.h>

#include <array>
#include <string>
#include <vector>

namespace wallpaper_engine {

enum class SceneObjectKind {
    Unknown,
    Image,
    Particle,
};

struct SceneNodeDocument {
    bool valid = false;
    uint32_t id = 0;
    uint32_t parent_id = 0;
    std::array<float, 3> origin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> angles = {0.0f, 0.0f, 0.0f};
    std::array<float, 2> parallax_depth = {0.0f, 0.0f};
    bool has_parallax_depth = false;
    bool propagate_to_children = true;
};

struct EffectInstanceDocument {
    std::string file;
    bool visible = true;
    std::string instance_config_json;
};

struct ImageObjectDocument {
    struct AlphaKey {
        float frame = 0.0f;
        float value = 1.0f;
    };
    std::string image;
    std::string model;
    std::array<float, 2> size = {0.0f, 0.0f};
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    std::vector<AlphaKey> alpha_keys;
    float alpha_fps = 30.0f;
    float alpha_length = 0.0f;
    std::string alpha_mode;
    int color_blend_mode = 0;
    bool solid = false;
    bool copy_background = false;
};

struct ParticleObjectDocument {
    std::string particle;
    float override_alpha = 1.0f;
    float override_rate = 1.0f;
    std::array<float, 3> override_color = {1.0f, 1.0f, 1.0f};
    bool has_override_color = false;
    bool override_color_is_legacy = false;
};

struct SceneObjectDocument {
    SceneObjectKind kind = SceneObjectKind::Unknown;
    SceneNodeDocument node;
    std::string name;
    bool visible = true;

    ImageObjectDocument image;
    ParticleObjectDocument particle;
    std::vector<EffectInstanceDocument> effects;
};

struct SceneDocument {
    float design_width = 0.0f;
    float design_height = 0.0f;
    std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear_color = false;

    bool camera_parallax_enabled = false;
    float camera_parallax_amount = 0.0f;
    float camera_parallax_delay = 0.1f;
    float camera_parallax_mouse_influence = 0.0f;
    bool camera_shake_enabled = false;
    float camera_shake_amplitude = 0.0f;
    float camera_shake_speed = 0.0f;
    float camera_shake_roughness = 0.0f;
    float perspective_override_fov = 0.0f;

    std::vector<SceneObjectDocument> objects;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_SCENE_DOCUMENT_H
