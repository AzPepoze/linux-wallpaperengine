#ifndef WALLPAPER_ENGINE_SCENE_DOCUMENT_H
#define WALLPAPER_ENGINE_SCENE_DOCUMENT_H

#include <stdint.h>

#include <array>
#include <string>
#include <vector>

#include "wallpaper/2d/animation_curve.h"

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

// A scene property driven by a SceneScript: the module source and the scene's `scriptproperties` overrides as JSON.
struct ScriptedValue {
    std::string script;
    std::string properties_json;
    bool empty() const {
        return script.empty();
    }
};

struct SceneNodeDocument {
    bool valid = false;
    uint32_t id = 0;
    uint32_t parent_id = 0;
    std::string attachment;
    std::array<float, 3> origin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> angles = {0.0f, 0.0f, 0.0f};
    ScriptedValue origin_script, scale_script, angles_script;
    std::array<float, 2> parallax_depth = {0.0f, 0.0f};
    bool has_parallax_depth = false;
    bool propagate_to_children = true;
};

struct PropertyAnimationDocument {
    std::string property;               // "origin", "scale", "angles", "color" or "alpha"
    AnimationCurve curves[3];           // c0, c1, c2; alpha uses curves[0] only; a missing channel has no keys
    std::string name;                   // options.name, may be empty
    std::string parent;                 // options.parent.key, empty for a timeline root
    std::vector<std::string> children;  // options.children[].key
    bool start_paused = false;          // options.startpaused
    bool relative = false;              // animation.relative
};

// A material constant that carries a script, a keyframe animation, or both.
struct EffectConstantScript {
    int pass = 0;      // index into the effect's `passes`
    std::string name;  // the constant's key in constantshadervalues
    ScriptedValue script;
    PropertyAnimationDocument animation;  // `property` holds the constant's name
    bool has_animation = false;
};

struct EffectInstanceDocument {
    std::string file;
    std::string name;  // how scripts address the effect: getEffect("name")
    bool visible = true;
    ScriptedValue visible_script;
    std::string instance_config_json;
    std::vector<EffectConstantScript> constant_scripts;
};

struct AnimationLayerDocument {
    uint32_t animation = 0;
    std::string name;  // how scripts address the layer: getAnimationLayer("name")
    float rate = 1.0f;
    float blend = 1.0f;
    bool additive = false;
    bool visible = true;
};

struct ImageObjectDocument {
    using AlphaKey = CurveKeyframe;
    std::string image;
    std::string model;
    std::array<float, 2> size = {0.0f, 0.0f};
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    ScriptedValue color_script, size_script;
    float alpha = 1.0f;
    std::vector<AlphaKey> alpha_keys;
    float alpha_fps = 30.0f;
    float alpha_length = 0.0f;
    std::string alpha_mode;
    // SceneScript that drives alpha (`init()` start value, `update(value)` per frame).
    std::string alpha_script;
    std::string alpha_script_properties_json;
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
    float override_count = 1.0f;
    float override_speed = 1.0f;
    float override_lifetime = 1.0f;
    std::array<float, 3> override_color = {1.0f, 1.0f, 1.0f};
    bool has_override_color = false;
    bool override_color_is_legacy = false;
};

struct TextObjectDocument {
    std::string text;
    // SceneScript source (ES module) and the raw `scriptproperties` overrides.
    std::string script;
    std::string script_properties_json;
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

// One animated scene property. Properties of an object that link via parent/children share a single timeline.
struct SceneObjectDocument {
    SceneObjectKind kind = SceneObjectKind::Unknown;
    SceneNodeDocument node;
    std::string name;
    bool visible = true;
    ScriptedValue visible_script;

    ImageObjectDocument image;
    ParticleObjectDocument particle;
    TextObjectDocument text;
    SoundObjectDocument sound;
    std::vector<EffectInstanceDocument> effects;
    std::vector<PropertyAnimationDocument> animations;
    std::string raw_json;  // the object as authored (getInitialLayerConfig)
};

struct SceneCameraDocument {
    std::array<float, 3> center = {0.0f, 0.0f, -1.0f};
    std::array<float, 3> eye = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> up = {0.0f, 1.0f, 0.0f};

    // Camera-path objects animate the zoom and a relative origin offset (the scene's entry animation).
    AnimationCurve zoom_curve;
    std::array<AnimationCurve, 3> origin_curves;
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
