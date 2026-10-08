#include <cjson/cJSON.h>
#include <string.h>

#include <string>

#include "scene_parser_internal.h"

namespace wallpaper_engine {
namespace scene_parser_detail {

namespace {

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
    const cJSON* scale = cJSON_GetObjectItemCaseSensitive(object, "scale");
    if (!parseVec(scale, out.scale.data(), 3)) {
        float uniform = 1.0f;
        if (parseFloat(scale, uniform)) out.scale.fill(uniform);
    }
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
    parseFloat(member(object, "brightness"), text_doc.brightness);
    parseFloat(member(object, "backgroundbrightness"), text_doc.background_brightness);
    parseVec(member(object, "size"), text_doc.size.data(), 2);
    parseFloat(member(object, "maxwidth"), text_doc.maxwidth);
    text_doc.limit_width = parseBool(member(object, "limitwidth"), false);
    text_doc.limit_rows = parseBool(member(object, "limitrows"), false);
    float max_rows = 1.0f;
    if (parseFloat(member(object, "maxrows"), max_rows)) text_doc.max_rows = (int)max_rows;
    parseString(member(object, "horizontalalign"), text_doc.horizontal_align);
    parseString(member(object, "verticalalign"), text_doc.vertical_align);
    text_doc.opaque_background = parseBool(member(object, "opaquebackground"), false);
    parseVec(member(object, "backgroundcolor"), text_doc.background_color.data(), 3);
    parseFloat(member(object, "padding"), text_doc.padding);
    parseString(member(object, "anchor"), text_doc.anchor);
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

}  // namespace

SceneObjectDocument parseObject(const cJSON* object) {
    SceneObjectDocument doc;
    doc.kind = detectObjectKind(object);
    doc.node = parseNode(object);
    readPlainString(object, "name", doc.name);
    readPlainString(object, "alignment", doc.alignment);
    doc.visible = parseBool(member(object, "visible"), true);
    readScript(member(object, "visible"), doc.visible_script);

    parseImageFields(object, doc.image);
    parseParticleFields(object, doc.particle);
    parseTextFields(object, doc.text);
    parseSoundFields(object, doc.sound);
    parseEffects(object, doc.effects);
    for (const char* property : {"origin", "scale", "angles", "color", "alpha"}) {
        parsePropertyAnimation(object, property, doc.animations);
    }
    if (char* serialized = cJSON_PrintUnformatted(object)) {
        doc.raw_json = serialized;
        cJSON_free(serialized);
    }
    return doc;
}

}  // namespace scene_parser_detail
}  // namespace wallpaper_engine
