#ifndef SCRIPT_CORPUS_FAKE_SCENE_H
#define SCRIPT_CORPUS_FAKE_SCENE_H

#include <cjson/cJSON.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "wallpaper/2d/animation/animation_timelines.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/script/scene_script.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/2d/script/script_scene_backend.h"

namespace script_corpus {

// The scene's objects as a script sees them, so thisLayer / thisScene work without a renderer.
struct FakeLayer {
    uint32_t id = 0, parent = 0;
    std::string name;
    double origin[3] = {0, 0, 0}, scale[3] = {1, 1, 1}, angles[3] = {0, 0, 0}, size[2] = {0, 0};
    bool visible = true;
    bool text = false, sound = false, sound_playing = false;
    std::map<std::string, std::string> strings;
    std::map<std::string, double> numbers = {{"alpha", 1.0}};
};

// "x y z" strings, optionally wrapped as { "value": ... } by a script-driven or user-bound property.
inline void readVector(const cJSON* property, double* out, int count) {
    if (cJSON_IsObject(property)) property = cJSON_GetObjectItemCaseSensitive(property, "value");
    if (!cJSON_IsString(property) || !property->valuestring) return;
    double v[3] = {out[0], out[1], count > 2 ? out[2] : 0.0};
    const int read = sscanf(property->valuestring, "%lf %lf %lf", &v[0], &v[1], &v[2]);
    for (int i = 0; i < count && i < read; ++i) out[i] = v[i];
}

class FakeScene : public ScriptSceneBackend {
   public:
    void load(const cJSON* scene) {
        layers_.clear();
        const cJSON* objects = cJSON_GetObjectItemCaseSensitive(scene, "objects");
        const cJSON* object = nullptr;
        cJSON_ArrayForEach(object, objects) {
            FakeLayer layer;
            const cJSON* id = cJSON_GetObjectItemCaseSensitive(object, "id");
            const cJSON* parent = cJSON_GetObjectItemCaseSensitive(object, "parent");
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(object, "name");
            if (cJSON_IsNumber(id)) layer.id = (uint32_t)id->valuedouble;
            if (cJSON_IsNumber(parent)) layer.parent = (uint32_t)parent->valuedouble;
            if (cJSON_IsString(name) && name->valuestring) layer.name = name->valuestring;
            readVector(cJSON_GetObjectItemCaseSensitive(object, "origin"), layer.origin, 3);
            readVector(cJSON_GetObjectItemCaseSensitive(object, "scale"), layer.scale, 3);
            readVector(cJSON_GetObjectItemCaseSensitive(object, "angles"), layer.angles, 3);
            for (double& angle : layer.angles) angle *= 180.0 / M_PI;  // stored in radians, scripts see degrees
            readVector(cJSON_GetObjectItemCaseSensitive(object, "size"), layer.size, 2);
            const cJSON* visible = cJSON_GetObjectItemCaseSensitive(object, "visible");
            if (cJSON_IsObject(visible)) visible = cJSON_GetObjectItemCaseSensitive(visible, "value");
            if (cJSON_IsBool(visible)) layer.visible = cJSON_IsTrue(visible);
            if (layer.id != 0) layers_.push_back(layer);
        }
    }
    FakeLayer* find(uint32_t id) {
        for (FakeLayer& layer : layers_)
            if (layer.id == id) return &layer;
        return nullptr;
    }

    // Keyframe animations come from the real parser, so scripts see the same timelines as in the engine.
    void loadAnimations(const wallpaper_engine::SceneDocument& document) {
        timelines_ = AnimationTimelines();
        for (const auto& object : document.objects)
            if (object.node.valid && !object.animations.empty()) timelines_.add(object.node.id, object.animations);
    }
    void advance(float dt) {
        timelines_.update(dt, [this](const AnimatedValue& animated) {
            FakeLayer* layer = find(animated.object_id);
            double* target = !layer                          ? nullptr
                             : animated.property == "origin" ? layer->origin
                             : animated.property == "scale"  ? layer->scale
                             : animated.property == "angles" ? layer->angles
                                                             : nullptr;
            if (!target) return;
            const double factor = animated.property == "angles" ? 180.0 / M_PI : 1.0;
            for (int i = 0; i < 3; ++i)
                if (animated.has[i]) target[i] = animated.value[i] * factor;
        });
        for (uint32_t handle : timelines_.takeEnded()) ScriptEngine::instance().animationEnded(handle);
    }
    struct FakeAnimationLayer {
        uint32_t layer_id = 0;
        std::string name;
        double rate = 1, blend = 1, frame = 0;
        bool visible = true, playing = true;
    };
    static constexpr uint32_t kLayerBase = 0x40000000u;

    void loadAnimationLayers(const wallpaper_engine::SceneDocument& document) {
        animation_layers_.clear();
        for (const auto& object : document.objects) {
            for (const auto& entry : object.image.animation_layers) {
                FakeAnimationLayer layer;
                layer.layer_id = object.node.id;
                layer.name = entry.name;
                layer.rate = entry.rate;
                layer.blend = entry.blend;
                layer.visible = entry.visible;
                animation_layers_.push_back(layer);
            }
        }
    }
    FakeAnimationLayer* animationLayer(uint32_t handle) {
        if (handle < kLayerBase || handle - kLayerBase >= animation_layers_.size()) return nullptr;
        return &animation_layers_[handle - kLayerBase];
    }

    uint32_t findAnimation(uint32_t layer_id, const std::string& kind, const std::string& key) override {
        if (kind == "timeline" || kind == "any") {
            if (const uint32_t handle = timelines_.find(layer_id, key)) return handle;
            if (kind == "timeline") return 0;
        }
        if (kind == "layer" || kind == "any") {
            int ordinal = 0;
            for (size_t i = 0; i < animation_layers_.size(); ++i) {
                if (animation_layers_[i].layer_id != layer_id) continue;
                if (animation_layers_[i].name == key || (kind == "layer" && std::to_string(ordinal) == key))
                    return kLayerBase + (uint32_t)i;
                ++ordinal;
            }
        }
        return 0;
    }
    bool animationGet(uint32_t handle, const std::string& field, double& out) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.get(handle, field, out);
        if (field == "rate")
            out = layer->rate;
        else if (field == "blend")
            out = layer->blend;
        else if (field == "visible")
            out = layer->visible ? 1 : 0;
        else if (field == "playing")
            out = layer->playing ? 1 : 0;
        else if (field == "frame")
            out = layer->frame;
        else
            return false;
        return true;
    }
    bool animationGetString(uint32_t handle, const std::string& field, std::string& out) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.getString(handle, field, out);
        if (field != "name") return false;
        out = layer->name;
        return true;
    }
    bool animationSet(uint32_t handle, const std::string& field, double value) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.set(handle, field, value);
        if (field == "rate")
            layer->rate = value;
        else if (field == "blend")
            layer->blend = value;
        else if (field == "visible")
            layer->visible = value != 0;
        else if (field == "frame")
            layer->frame = value;
        else
            return false;
        return true;
    }
    bool animationCommand(uint32_t handle, const std::string& command) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.command(handle, command);
        if (command == "play")
            layer->playing = true;
        else if (command == "pause")
            layer->playing = false;
        else if (command == "stop")
            layer->playing = false, layer->frame = 0;
        else
            return false;
        return true;
    }
    int animationLayerCount(uint32_t layer_id) override {
        int count = 0;
        for (const FakeAnimationLayer& layer : animation_layers_) count += layer.layer_id == layer_id;
        return count;
    }

    bool layerExists(uint32_t id) override {
        return find(id) != nullptr;
    }
    std::string layerName(uint32_t id) override {
        FakeLayer* layer = find(id);
        return layer ? layer->name : "";
    }
    bool getVector(uint32_t id, const std::string& property, double out[3], int& components) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        const double* source = property == "origin"   ? layer->origin
                               : property == "scale"  ? layer->scale
                               : property == "angles" ? layer->angles
                                                      : nullptr;
        if (source) {
            for (int i = 0; i < 3; ++i) out[i] = source[i];
            components = 3;
            return true;
        }
        if (property == "size") {
            out[0] = layer->size[0];
            out[1] = layer->size[1];
            components = 2;
            return true;
        }
        return false;
    }
    bool setVector(uint32_t id, const std::string& property, const double value[3]) override {
        FakeLayer* layer = find(id);
        double* target = !layer                 ? nullptr
                         : property == "origin" ? layer->origin
                         : property == "scale"  ? layer->scale
                         : property == "angles" ? layer->angles
                                                : nullptr;
        if (!target) return false;
        for (int i = 0; i < 3; ++i) target[i] = value[i];
        return true;
    }
    void loadKinds(const wallpaper_engine::SceneDocument& document) {
        for (const auto& object : document.objects) {
            FakeLayer* layer = find(object.node.id);
            if (!layer) continue;
            if (object.kind == wallpaper_engine::SceneObjectKind::Text) {
                layer->text = true;
                layer->strings = {
                    {"text", ""}, {"font", ""}, {"horizontalalign", "center"}, {"verticalalign", "center"}};
                layer->numbers.insert({{"pointsize", 12}, {"maxwidth", 0}, {"maxrows", 0}});
            } else if (object.kind == wallpaper_engine::SceneObjectKind::Sound) {
                layer->sound = true;
                layer->numbers["volume"] = object.sound.volume;
            }
        }
    }
    bool getWorldMatrix(uint32_t id, double out[16]) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        for (int i = 0; i < 16; ++i) out[i] = i % 5 == 0 ? 1.0 : 0.0;
        for (int i = 0; i < 3; ++i) out[12 + i] = layer->origin[i];
        return true;
    }
    bool layerCommand(uint32_t id, const std::string& command) override {
        FakeLayer* layer = find(id);
        if (!layer || !layer->sound) return false;
        layer->sound_playing = command == "play";
        return true;
    }
    bool getNumber(uint32_t id, const std::string& property, double& out) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        const auto it = layer->numbers.find(property);
        if (it == layer->numbers.end()) return false;
        out = it->second;
        return true;
    }
    bool setNumber(uint32_t id, const std::string& property, double value) override {
        FakeLayer* layer = find(id);
        if (!layer || !layer->numbers.count(property)) return false;
        layer->numbers[property] = value;
        return true;
    }
    bool getString(uint32_t id, const std::string& property, std::string& out) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        const auto it = layer->strings.find(property);
        if (it == layer->strings.end()) return false;
        out = it->second;
        return true;
    }
    bool setString(uint32_t id, const std::string& property, const std::string& value) override {
        FakeLayer* layer = find(id);
        if (!layer || !layer->strings.count(property)) return false;
        layer->strings[property] = value;
        return true;
    }
    bool getBool(uint32_t id, const std::string& property, bool& out) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        if (property == "visible") {
            out = layer->visible;
            return true;
        }
        if (property == "playing" && layer->sound) {
            out = layer->sound_playing;
            return true;
        }
        return false;
    }
    bool setBool(uint32_t id, const std::string& property, bool value) override {
        FakeLayer* layer = find(id);
        if (!layer || property != "visible") return false;
        layer->visible = value;
        return true;
    }
    uint32_t parentOf(uint32_t id) override {
        FakeLayer* layer = find(id);
        return layer ? layer->parent : 0;
    }
    std::vector<uint32_t> childrenOf(uint32_t id) override {
        std::vector<uint32_t> children;
        for (const FakeLayer& layer : layers_)
            if (layer.parent == id) children.push_back(layer.id);
        return children;
    }
    uint32_t findLayerByName(const std::string& name) override {
        for (const FakeLayer& layer : layers_)
            if (layer.name == name) return layer.id;
        return 0;
    }
    std::vector<uint32_t> allLayers() override {
        std::vector<uint32_t> ids;
        for (const FakeLayer& layer : layers_) ids.push_back(layer.id);
        return ids;
    }

   private:
    std::vector<FakeLayer> layers_;
    AnimationTimelines timelines_;
    std::vector<FakeAnimationLayer> animation_layers_;
};

}  // namespace script_corpus

#endif  // SCRIPT_CORPUS_FAKE_SCENE_H
