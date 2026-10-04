#include "scene_parser.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utility>

#include "shared/core/config.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"

namespace wallpaper_engine {
namespace {
const cJSON* propertyValue(const cJSON* node) {
    if (cJSON_IsObject(node)) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(node, "value");
        if (value) return value;
    }
    return node;
}

bool parseVec(const cJSON* raw, float* out, int count) {
    const cJSON* node = propertyValue(raw);
    if (!node || !out || count <= 0) return false;

    if (cJSON_IsString(node) && node->valuestring) {
        if (count == 2) return sscanf(node->valuestring, "%f %f", &out[0], &out[1]) == 2;
        if (count == 3) return sscanf(node->valuestring, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
        return false;
    }

    if (cJSON_IsArray(node) && cJSON_GetArraySize(node) >= count) {
        for (int i = 0; i < count; ++i) {
            const cJSON* value = cJSON_GetArrayItem(node, i);
            if (!cJSON_IsNumber(value)) return false;
            out[i] = (float)value->valuedouble;
        }
        return true;
    }

    return false;
}

bool parseFloat(const cJSON* raw, float& out) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsNumber(node)) {
        out = (float)node->valuedouble;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        char* end = nullptr;
        const float value = strtof(node->valuestring, &end);
        if (end != node->valuestring) {
            out = value;
            return true;
        }
    }
    return false;
}

bool parseBool(const cJSON* raw, bool fallback = false) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsBool(node)) return cJSON_IsTrue(node);
    if (cJSON_IsNumber(node)) return node->valuedouble != 0.0;
    return fallback;
}

bool parseString(const cJSON* raw, std::string& out) {
    const cJSON* node = propertyValue(raw);
    if (cJSON_IsString(node) && node->valuestring) {
        out = node->valuestring;
        return true;
    }
    return false;
}

// Reads `script` and `scriptproperties` from a property object; leaves `out` empty for plain values.
void readScript(const cJSON* property, ScriptedValue& out) {
    if (!cJSON_IsObject(property)) return;
    const cJSON* script = cJSON_GetObjectItemCaseSensitive(property, "script");
    if (!cJSON_IsString(script) || !script->valuestring || !script->valuestring[0]) return;
    out.script = script->valuestring;
    const cJSON* properties = cJSON_GetObjectItemCaseSensitive(property, "scriptproperties");
    if (cJSON_IsObject(properties)) {
        if (char* printed = cJSON_PrintUnformatted(properties)) {
            out.properties_json = printed;
            cJSON_free(printed);
        }
    }
}

SceneNodeDocument parseNode(const cJSON* object) {
    SceneNodeDocument out;

    const cJSON* id = cJSON_GetObjectItemCaseSensitive(object, "id");
    if (!cJSON_IsNumber(id) || id->valuedouble <= 0.0) return out;

    out.valid = true;
    out.id = (uint32_t)id->valuedouble;

    const cJSON* parent = cJSON_GetObjectItemCaseSensitive(object, "parent");
    if (cJSON_IsNumber(parent) && parent->valuedouble > 0.0) {
        out.parent_id = (uint32_t)parent->valuedouble;
    }
    parseString(cJSON_GetObjectItemCaseSensitive(object, "attachment"), out.attachment);

    parseVec(cJSON_GetObjectItemCaseSensitive(object, "origin"), out.origin.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(object, "scale"), out.scale.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(object, "angles"), out.angles.data(), 3);
    readScript(cJSON_GetObjectItemCaseSensitive(object, "origin"), out.origin_script);
    readScript(cJSON_GetObjectItemCaseSensitive(object, "scale"), out.scale_script);
    readScript(cJSON_GetObjectItemCaseSensitive(object, "angles"), out.angles_script);

    out.has_parallax_depth =
        parseVec(cJSON_GetObjectItemCaseSensitive(object, "parallaxDepth"), out.parallax_depth.data(), 2);

    if (!out.has_parallax_depth) {
        const cJSON* image = cJSON_GetObjectItemCaseSensitive(object, "image");
        if (cJSON_IsString(image) && image->valuestring &&
            strcmp(image->valuestring, "models/util/composelayer.json") == 0) {
            out.parallax_depth = {1.0f, 1.0f};
        }
    }

    out.propagate_to_children = !parseBool(cJSON_GetObjectItemCaseSensitive(object, "disablepropagation"), false);
    return out;
}

SceneObjectKind detectObjectKind(const cJSON* object) {
    const cJSON* sound = cJSON_GetObjectItemCaseSensitive(object, "sound");
    if (cJSON_IsArray(sound) || cJSON_IsString(sound)) return SceneObjectKind::Sound;

    const cJSON* particle = cJSON_GetObjectItemCaseSensitive(object, "particle");
    if (cJSON_IsString(particle)) return SceneObjectKind::Particle;

    const cJSON* text = cJSON_GetObjectItemCaseSensitive(object, "text");
    if (cJSON_IsString(text) || cJSON_IsObject(text)) return SceneObjectKind::Text;

    const cJSON* image = cJSON_GetObjectItemCaseSensitive(object, "image");
    const cJSON* model = cJSON_GetObjectItemCaseSensitive(object, "model");
    if (cJSON_IsString(image) || cJSON_IsString(model)) return SceneObjectKind::Image;

    return SceneObjectKind::Unknown;
}

void detectResolution(const cJSON* root, SceneDocument& out) {
    const cJSON* resolution = cJSON_GetObjectItemCaseSensitive(root, "resolution");
    if (cJSON_IsString(resolution)) {
        sscanf(resolution->valuestring, "%f %f", &out.design_width, &out.design_height);
    }

    const cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    if (out.design_width == 0.0f && general) {
        const cJSON* ortho = cJSON_GetObjectItemCaseSensitive(general, "orthogonalprojection");
        if (ortho) {
            const cJSON* width = cJSON_GetObjectItemCaseSensitive(ortho, "width");
            const cJSON* height = cJSON_GetObjectItemCaseSensitive(ortho, "height");
            if (cJSON_IsNumber(width)) out.design_width = (float)width->valuedouble;
            if (cJSON_IsNumber(height)) out.design_height = (float)height->valuedouble;
        }
    }

    if (out.design_width == 0.0f) {
        out.design_width = Config::kDefaultSceneWidth;
        out.design_height = Config::kDefaultSceneHeight;
        LOG_W("Design resolution not found, defaulting to %dx%d", (int)out.design_width, (int)out.design_height);
    } else {
        LOG_I("Detected Design Resolution: %.0fx%.0f", out.design_width, out.design_height);
    }
}

void parseCamera(const cJSON* camera, SceneDocument& out) {
    if (!camera) return;
    parseVec(cJSON_GetObjectItemCaseSensitive(camera, "center"), out.camera.center.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(camera, "eye"), out.camera.eye.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(camera, "up"), out.camera.up.data(), 3);
}

void parseGeneral(const cJSON* general, SceneDocument& out) {
    if (!general) return;

    parseVec(cJSON_GetObjectItemCaseSensitive(general, "ambientcolor"), out.general.ambient_color.data(), 3);
    parseVec(cJSON_GetObjectItemCaseSensitive(general, "skylightcolor"), out.general.skylight_color.data(), 3);

    const cJSON* clear_color = cJSON_GetObjectItemCaseSensitive(general, "clearcolor");
    if (cJSON_IsString(clear_color) && clear_color->valuestring) {
        float r, g, b;
        if (sscanf(clear_color->valuestring, "%f %f %f", &r, &g, &b) == 3) {
            out.general.clear_color = {r, g, b, 1.0f};
            out.general.has_clear_color = true;
        }
    }

    out.general.clear_enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "clearenabled"), true);
    out.general.hdr = parseBool(cJSON_GetObjectItemCaseSensitive(general, "hdr"), true);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "zoom"), out.general.zoom);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "fov"), out.general.fov);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "nearz"), out.general.near_z);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "farz"), out.general.far_z);
    out.general.camera_fade = parseBool(cJSON_GetObjectItemCaseSensitive(general, "camerafade"), true);
    out.general.camera_preview = parseBool(cJSON_GetObjectItemCaseSensitive(general, "camerapreview"), true);

    out.general.camera_parallax_enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "cameraparallax"), false);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxamount"), out.general.camera_parallax_amount);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxdelay"), out.general.camera_parallax_delay);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "cameraparallaxmouseinfluence"),
               out.general.camera_parallax_mouse_influence);

    out.general.camera_shake_enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "camerashake"), false);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "camerashakeamplitude"), out.general.camera_shake_amplitude);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "camerashakespeed"), out.general.camera_shake_speed);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "camerashakeroughness"), out.general.camera_shake_roughness);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "perspectiveoverridefov"),
               out.general.perspective_override_fov);

    const cJSON* ortho = cJSON_GetObjectItemCaseSensitive(general, "orthogonalprojection");
    if (cJSON_IsObject(ortho)) {
        parseFloat(cJSON_GetObjectItemCaseSensitive(ortho, "width"), out.general.orthogonal_projection[0]);
        parseFloat(cJSON_GetObjectItemCaseSensitive(ortho, "height"), out.general.orthogonal_projection[1]);
    }

    const bool is_hdr = parseBool(cJSON_GetObjectItemCaseSensitive(general, "hdr"), false);
    out.general.bloom.enabled = parseBool(cJSON_GetObjectItemCaseSensitive(general, "bloom"), false) || is_hdr;
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomstrength"), out.general.bloom.strength);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomthreshold"), out.general.bloom.threshold);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrfeather"), out.general.bloom.hdr_feather);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdriterations"), out.general.bloom.hdr_iterations);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrscatter"), out.general.bloom.hdr_scatter);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrstrength"), out.general.bloom.hdr_strength);
    parseFloat(cJSON_GetObjectItemCaseSensitive(general, "bloomhdrthreshold"), out.general.bloom.hdr_threshold);

    LOG_I("Camera Parallax: %s, amount=%.3f, delay=%.3f, mouse influence=%.3f",
          out.general.camera_parallax_enabled ? "enabled" : "disabled", out.general.camera_parallax_amount,
          out.general.camera_parallax_delay, out.general.camera_parallax_mouse_influence);
    LOG_I("Camera Shake: %s, amplitude=%.3f, speed=%.3f, roughness=%.3f",
          out.general.camera_shake_enabled ? "enabled" : "disabled", out.general.camera_shake_amplitude,
          out.general.camera_shake_speed, out.general.camera_shake_roughness);
    LOG_I("Scene General: ambient=[%.2f, %.2f, %.2f], skylight=[%.2f, %.2f, %.2f], fov=%.1f, zoom=%.2f, bloom=%s",
          out.general.ambient_color[0], out.general.ambient_color[1], out.general.ambient_color[2],
          out.general.skylight_color[0], out.general.skylight_color[1], out.general.skylight_color[2], out.general.fov,
          out.general.zoom, out.general.bloom.enabled ? "enabled" : "disabled");
}

void parseAnimationLayers(const cJSON* array, std::vector<AnimationLayerDocument>& out) {
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, array) {
        float animation = 0.0f;
        if (!parseFloat(cJSON_GetObjectItemCaseSensitive(entry, "animation"), animation)) continue;
        AnimationLayerDocument layer;
        layer.animation = (uint32_t)animation;
        parseFloat(cJSON_GetObjectItemCaseSensitive(entry, "rate"), layer.rate);
        parseFloat(cJSON_GetObjectItemCaseSensitive(entry, "blend"), layer.blend);
        layer.additive = parseBool(cJSON_GetObjectItemCaseSensitive(entry, "additive"), false);
        layer.visible = parseBool(cJSON_GetObjectItemCaseSensitive(entry, "visible"), true);
        out.push_back(layer);
    }
}

const cJSON* member(const cJSON* object, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

void readPlainString(const cJSON* object, const char* key, std::string& out) {
    const cJSON* node = member(object, key);
    if (cJSON_IsString(node) && node->valuestring) out = node->valuestring;
}

// `alpha` is either a number or an animated property: a fallback `value` plus keyframes under `animation`.
void parseImageAlpha(const cJSON* alpha, ImageObjectDocument& image) {
    if (cJSON_IsNumber(alpha)) {
        image.alpha = (float)alpha->valuedouble;
        return;
    }
    if (!cJSON_IsObject(alpha)) return;

    parseFloat(member(alpha, "value"), image.alpha);
    readPlainString(alpha, "script", image.alpha_script);
    if (const cJSON* properties = member(alpha, "scriptproperties"); cJSON_IsObject(properties)) {
        if (char* printed = cJSON_PrintUnformatted(properties)) {
            image.alpha_script_properties_json = printed;
            cJSON_free(printed);
        }
    }
    const cJSON* animation = member(alpha, "animation");
    if (!cJSON_IsObject(animation)) return;

    const cJSON* keys = member(animation, "c0");
    if (cJSON_IsArray(keys)) {
        const cJSON* key = nullptr;
        cJSON_ArrayForEach(key, keys) {
            ImageObjectDocument::AlphaKey parsed;
            if (!parseFloat(member(key, "frame"), parsed.frame) || !parseFloat(member(key, "value"), parsed.value))
                continue;
            image.alpha_keys.push_back(parsed);
        }
    }
    const cJSON* options = member(animation, "options");
    if (cJSON_IsObject(options)) {
        parseFloat(member(options, "fps"), image.alpha_fps);
        parseFloat(member(options, "length"), image.alpha_length);
        const cJSON* mode = member(options, "mode");
        if (cJSON_IsString(mode) && mode->valuestring) image.alpha_mode = mode->valuestring;
    }
}

void parseImageFields(const cJSON* object, ImageObjectDocument& image) {
    readPlainString(object, "image", image.image);
    readPlainString(object, "model", image.model);
    parseVec(member(object, "size"), image.size.data(), 2);
    parseVec(member(object, "color"), image.color.data(), 3);
    readScript(member(object, "size"), image.size_script);
    readScript(member(object, "color"), image.color_script);
    parseImageAlpha(member(object, "alpha"), image);
    const cJSON* color_blend_mode = member(object, "colorBlendMode");
    if (cJSON_IsNumber(color_blend_mode)) image.color_blend_mode = (int)color_blend_mode->valuedouble;
    image.solid = parseBool(member(object, "solid"), false);
    image.copy_background = parseBool(member(object, "copybackground"), false);
    parseAnimationLayers(member(object, "animationlayers"), image.animation_layers);
}

void parseParticleFields(const cJSON* object, ParticleObjectDocument& particle) {
    readPlainString(object, "particle", particle.particle);

    const cJSON* instance_override = member(object, "instanceoverride");
    if (!cJSON_IsObject(instance_override)) return;
    parseFloat(member(instance_override, "alpha"), particle.override_alpha);
    parseFloat(member(instance_override, "rate"), particle.override_rate);
    parseFloat(member(instance_override, "size"), particle.override_size);
    parseFloat(member(instance_override, "count"), particle.override_count);
    parseFloat(member(instance_override, "speed"), particle.override_speed);
    parseFloat(member(instance_override, "lifetime"), particle.override_lifetime);

    if (parseVec(member(instance_override, "color"), particle.override_color.data(), 3)) {
        particle.has_override_color = true;
        particle.override_color_is_legacy = true;
    } else if (parseVec(member(instance_override, "colorn"), particle.override_color.data(), 3)) {
        particle.has_override_color = true;
        particle.override_color_is_legacy = false;
    }
}

void parseTextFields(const cJSON* object, TextObjectDocument& text_doc) {
    const cJSON* text = member(object, "text");
    if (text) {
        // Scripted and user-bound text falls back to the authoring-time default in `value`.
        parseString(text, text_doc.text);
        readPlainString(text, "script", text_doc.script);
        const cJSON* script_props = member(text, "scriptproperties");
        if (cJSON_IsObject(script_props)) {
            if (char* printed = cJSON_PrintUnformatted(script_props)) {
                text_doc.script_properties_json = printed;
                cJSON_free(printed);
            }
        }
    }
    parseString(member(object, "font"), text_doc.font);
    parseFloat(member(object, "pointsize"), text_doc.pointsize);
    parseVec(member(object, "color"), text_doc.color.data(), 3);
    parseFloat(member(object, "alpha"), text_doc.alpha);
    parseVec(member(object, "size"), text_doc.size.data(), 2);
    parseFloat(member(object, "maxwidth"), text_doc.maxwidth);
    text_doc.limit_width = parseBool(member(object, "limitwidth"), false);
    text_doc.limit_rows = parseBool(member(object, "limitrows"), false);
    float max_rows = 1.0f;
    if (parseFloat(member(object, "maxrows"), max_rows)) text_doc.max_rows = (int)max_rows;
    parseString(member(object, "horizontalalign"), text_doc.horizontal_align);
    parseString(member(object, "verticalalign"), text_doc.vertical_align);
}

void parseSoundFields(const cJSON* object, SoundObjectDocument& sound_doc) {
    const cJSON* sound = member(object, "sound");
    if (cJSON_IsArray(sound)) {
        const cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, sound) {
            if (cJSON_IsString(entry) && entry->valuestring) sound_doc.sounds.emplace_back(entry->valuestring);
        }
    } else if (cJSON_IsString(sound) && sound->valuestring) {
        sound_doc.sounds.emplace_back(sound->valuestring);
    }
    std::string playback_mode;
    if (parseString(member(object, "playbackmode"), playback_mode)) {
        if (playback_mode == "loop")
            sound_doc.playback_mode = SoundPlaybackMode::Loop;
        else if (playback_mode == "random")
            sound_doc.playback_mode = SoundPlaybackMode::Random;
        else
            sound_doc.playback_mode = SoundPlaybackMode::Single;
    }
    parseFloat(member(object, "volume"), sound_doc.volume);
    sound_doc.mute = parseBool(member(object, "mute"), false);
    sound_doc.start_silent = parseBool(member(object, "startsilent"), false);
    parseFloat(member(object, "mintime"), sound_doc.min_time);
    parseFloat(member(object, "maxtime"), sound_doc.max_time);
}

void parseEffects(const cJSON* object, std::vector<EffectInstanceDocument>& out) {
    const cJSON* effects = member(object, "effects");
    if (!cJSON_IsArray(effects)) return;
    const cJSON* effect_json = nullptr;
    cJSON_ArrayForEach(effect_json, effects) {
        const cJSON* file = member(effect_json, "file");
        if (!cJSON_IsString(file) || !file->valuestring) continue;

        EffectInstanceDocument effect;
        effect.file = file->valuestring;
        effect.visible = parseBool(member(effect_json, "visible"), true);
        if (char* serialized = cJSON_PrintUnformatted(effect_json)) {
            effect.instance_config_json = serialized;
            cJSON_free(serialized);
        }
        out.push_back(std::move(effect));
    }
}

void parseCurve(const cJSON* animation, const char* channel, AnimationCurve& out) {
    const cJSON* keys = member(animation, channel);
    if (!cJSON_IsArray(keys)) return;
    const cJSON* key = nullptr;
    cJSON_ArrayForEach(key, keys) {
        CurveKeyframe parsed;
        if (!parseFloat(member(key, "frame"), parsed.frame) || !parseFloat(member(key, "value"), parsed.value))
            continue;
        out.keys.push_back(parsed);
    }
    const cJSON* options = member(animation, "options");
    if (!cJSON_IsObject(options)) return;
    parseFloat(member(options, "fps"), out.fps);
    parseFloat(member(options, "length"), out.length);
    const cJSON* mode = member(options, "mode");
    if (cJSON_IsString(mode) && mode->valuestring) out.mode = mode->valuestring;
}

// A visible camera object may carry keyframed `zoom` / `origin` properties.
void parseCameraPath(const cJSON* object, SceneCameraDocument& out) {
    if (!cJSON_IsString(member(object, "camera"))) return;
    if (!parseBool(member(object, "visible"), true)) return;

    const cJSON* zoom = member(member(object, "zoom"), "animation");
    if (cJSON_IsObject(zoom)) parseCurve(zoom, "c0", out.zoom_curve);
    const cJSON* origin = member(member(object, "origin"), "animation");
    if (cJSON_IsObject(origin)) {
        parseCurve(origin, "c0", out.origin_curves[0]);
        parseCurve(origin, "c1", out.origin_curves[1]);
        parseCurve(origin, "c2", out.origin_curves[2]);
    }
}

SceneObjectDocument parseObject(const cJSON* object) {
    SceneObjectDocument doc;
    doc.kind = detectObjectKind(object);
    doc.node = parseNode(object);
    readPlainString(object, "name", doc.name);
    doc.visible = parseBool(member(object, "visible"), true);
    readScript(member(object, "visible"), doc.visible_script);

    parseImageFields(object, doc.image);
    parseParticleFields(object, doc.particle);
    parseTextFields(object, doc.text);
    parseSoundFields(object, doc.sound);
    parseEffects(object, doc.effects);
    return doc;
}

}  // namespace

bool parseSceneFile(const char* scene_json_path, SceneDocument& out) {
    char* json_str = read_file_to_string(scene_json_path);
    if (!json_str) {
        LOG_E("Failed to read scene JSON: %s", scene_json_path);
        return false;
    }

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        LOG_E("Failed to parse scene JSON");
        return false;
    }

    LOG_I("Scene JSON parsed successfully");
    detectResolution(root, out);
    parseCamera(cJSON_GetObjectItemCaseSensitive(root, "camera"), out);
    parseGeneral(cJSON_GetObjectItemCaseSensitive(root, "general"), out);

    const cJSON* objects = cJSON_GetObjectItemCaseSensitive(root, "objects");
    if (cJSON_IsArray(objects)) {
        const cJSON* object;
        cJSON_ArrayForEach(object, objects) {
            parseCameraPath(object, out.camera);
            out.objects.push_back(parseObject(object));
        }
    }

    cJSON_Delete(root);
    return true;
}

}  // namespace wallpaper_engine
