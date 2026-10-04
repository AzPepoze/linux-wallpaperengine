#include <cjson/cJSON.h>

#include <utility>

#include "scene_parser_internal.h"

namespace wallpaper_engine {
namespace scene_parser_detail {

namespace {

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

}  // namespace

void parseAnimationLayers(const cJSON* array, std::vector<AnimationLayerDocument>& out) {
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, array) {
        float animation = 0.0f;
        if (!parseFloat(cJSON_GetObjectItemCaseSensitive(entry, "animation"), animation)) continue;
        AnimationLayerDocument layer;
        layer.animation = (uint32_t)animation;
        if (const cJSON* name = cJSON_GetObjectItemCaseSensitive(entry, "name");
            cJSON_IsString(name) && name->valuestring)
            layer.name = name->valuestring;
        parseFloat(cJSON_GetObjectItemCaseSensitive(entry, "rate"), layer.rate);
        parseFloat(cJSON_GetObjectItemCaseSensitive(entry, "blend"), layer.blend);
        layer.additive = parseBool(cJSON_GetObjectItemCaseSensitive(entry, "additive"), false);
        layer.visible = parseBool(cJSON_GetObjectItemCaseSensitive(entry, "visible"), true);
        out.push_back(layer);
    }
}
// Reads a keyframed property (`origin`, `scale`, `angles`, `color` or `alpha`) into `out` when it has channels.
void parsePropertyAnimation(const cJSON* object, const char* property, std::vector<PropertyAnimationDocument>& out) {
    const cJSON* prop = member(object, property);
    if (!cJSON_IsObject(prop)) return;
    const cJSON* animation = member(prop, "animation");
    if (!cJSON_IsObject(animation)) return;

    const char* const channels[] = {"c0", "c1", "c2"};
    PropertyAnimationDocument doc;
    doc.property = property;
    bool any_keys = false;
    for (int i = 0; i < 3; ++i) {
        parseCurve(animation, channels[i], doc.curves[i]);
        any_keys = any_keys || !doc.curves[i].keys.empty();
    }
    if (!any_keys) return;

    const cJSON* options = member(animation, "options");
    if (cJSON_IsObject(options)) {
        readPlainString(options, "name", doc.name);
        doc.start_paused = parseBool(member(options, "startpaused"), false);
        const cJSON* parent = member(options, "parent");
        if (cJSON_IsObject(parent)) readPlainString(parent, "key", doc.parent);
        const cJSON* children = member(options, "children");
        if (cJSON_IsArray(children)) {
            const cJSON* child = nullptr;
            cJSON_ArrayForEach(child, children) {
                std::string key;
                readPlainString(child, "key", key);
                if (!key.empty()) doc.children.push_back(std::move(key));
            }
        }
    }
    doc.relative = parseBool(member(animation, "relative"), false);
    out.push_back(std::move(doc));
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

}  // namespace scene_parser_detail
}  // namespace wallpaper_engine
