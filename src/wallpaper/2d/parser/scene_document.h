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
    Text,
    Sound,
};

enum class SoundPlaybackMode {
    Single,
    Loop,
    Random,
};

struct SceneNodeDocument {
    bool valid = false;
    uint32_t id = 0;
    uint32_t parent_id = 0;
    std::string attachment;
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

struct AnimationLayerDocument {
    uint32_t animation = 0;
    float rate = 1.0f;
    float blend = 1.0f;
    bool additive = false;
    bool visible = true;
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
    std::vector<AnimationLayerDocument> animation_layers;
};

struct ParticleObjectDocument {
    std::string particle;
    float override_alpha = 1.0f;
    float override_rate = 1.0f;
    float override_size = 1.0f;
    std::array<float, 3> override_color = {1.0f, 1.0f, 1.0f};
    bool has_override_color = false;
    bool override_color_is_legacy = false;
};

struct TextObjectDocument {
    std::string text;
    std::string font;
    float pointsize = 12.0f;
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    std::array<float, 2> size = {0.0f, 0.0f};
    float maxwidth = 0.0f;
    bool limit_width = false;
    bool limit_rows = false;
    int max_rows = 1;
    std::string horizontal_align = "center";
    std::string vertical_align = "center";
};

struct SoundObjectDocument {
    std::vector<std::string> sounds;
    SoundPlaybackMode playback_mode = SoundPlaybackMode::Single;
    float volume = 1.0f;
    bool mute = false;
    bool start_silent = false;
    float min_time = 1.0f;
    float max_time = 5.0f;
};

struct SceneObjectDocument {
    SceneObjectKind kind = SceneObjectKind::Unknown;
    SceneNodeDocument node;
    std::string name;
    bool visible = true;

    ImageObjectDocument image;
    ParticleObjectDocument particle;
    TextObjectDocument text;
    SoundObjectDocument sound;
    std::vector<EffectInstanceDocument> effects;
};

struct SceneCameraDocument {
    std::array<float, 3> center = {0.0f, 0.0f, -1.0f};
    std::array<float, 3> eye = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> up = {0.0f, 1.0f, 0.0f};
};

struct SceneBloomDocument {
    bool enabled = false;
    float strength = 1.0f;
    float threshold = 0.8f;
    float hdr_feather = 0.25f;
    float hdr_iterations = 8.0f;
    float hdr_scatter = 2.0f;
    float hdr_strength = 2.0f;
    float hdr_threshold = 1.0f;
};

struct SceneGeneralDocument {
    std::array<float, 3> ambient_color = {0.3f, 0.3f, 0.3f};
    std::array<float, 3> skylight_color = {0.3f, 0.3f, 0.3f};
    std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear_color = false;
    bool clear_enabled = true;
    bool hdr = true;
    float zoom = 1.0f;
    float fov = 50.0f;
    float near_z = 0.01f;
    float far_z = 10000.0f;
    bool camera_fade = true;
    bool camera_preview = true;

    bool camera_parallax_enabled = false;
    float camera_parallax_amount = 0.25f;
    float camera_parallax_delay = 0.1f;
    float camera_parallax_mouse_influence = 0.05f;

    bool camera_shake_enabled = false;
    float camera_shake_amplitude = 0.5f;
    float camera_shake_speed = 0.75f;
    float camera_shake_roughness = 1.0f;

    float perspective_override_fov = 0.0f;
    std::array<float, 2> orthogonal_projection = {0.0f, 0.0f};

    SceneBloomDocument bloom;
};

struct SceneDocument {
    float design_width = 0.0f;
    float design_height = 0.0f;

    SceneCameraDocument camera;
    SceneGeneralDocument general;

    std::vector<SceneObjectDocument> objects;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_SCENE_DOCUMENT_H
