#include "scene_scripts.h"

#include <algorithm>
#include <cctype>
#include <optional>

#include "script_engine.h"
#include "shared/core/logger.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/sound/sound_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

#define TAG "SCRIPT"

Layer* SceneScriptBackend::layerById(uint32_t id) const {
    if (id == 0) return nullptr;
    for (Layer* layer : ctx_.scene.layers)
        if (layer->scene_object_id == id) return layer;
    return nullptr;
}

bool SceneScriptBackend::layerExists(uint32_t id) {
    return id != 0 && ctx_.scene.scene_tree && ctx_.scene.scene_tree->find(id) != nullptr;
}

std::string SceneScriptBackend::layerName(uint32_t id) {
    if (Layer* layer = layerById(id)) return layer->name;
    if (ctx_.scene.scene_tree)
        if (const SceneTreeNode* node = ctx_.scene.scene_tree->find(id)) return node->name;
    return "";
}

bool SceneScriptBackend::getVector(uint32_t id, const std::string& property, double out[3], int& components) {
    const SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
    if (!node) return false;
    auto copy3 = [&](const std::array<float, 3>& v) {
        for (int i = 0; i < 3; ++i) out[i] = v[i];
        components = 3;
        return true;
    };
    if (property == "origin") return copy3(node->origin);
    if (property == "scale") return copy3(node->scale);
    if (property == "angles") return copy3(node->angles);
    if (property == "parallaxDepth") {
        out[0] = node->parallax_depth[0];
        out[1] = node->parallax_depth[1];
        components = 2;
        return true;
    }
    if (property == "size") {
        const Layer* layer = layerById(id);
        if (!layer) return false;
        out[0] = layer->size[0];
        out[1] = layer->size[1];
        components = 2;
        return true;
    }
    if (property == "color") {
        const Layer* layer = layerById(id);
        if (!layer) return false;
        for (int i = 0; i < 3; ++i) out[i] = layer->tint[i];
        components = 3;
        return true;
    }
    return false;
}

bool SceneScriptBackend::getWorldMatrix(uint32_t id, double out[16]) {
    mat4x4 world;
    if (!ctx_.scene.scene_tree || !ctx_.scene.scene_tree->worldTransform(id, world)) return false;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) out[column * 4 + row] = world[column][row];
    return true;
}

bool SceneScriptBackend::setVector(uint32_t id, const std::string& property, const double value[3]) {
    SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
    if (!node) return false;
    auto assign3 = [&](std::array<float, 3>& v) {
        for (int i = 0; i < 3; ++i) v[i] = (float)value[i];
        return true;
    };
    if (property == "origin") return assign3(node->origin);
    if (property == "scale") return assign3(node->scale);
    if (property == "angles") return assign3(node->angles);
    if (property == "parallaxDepth") {
        node->parallax_depth = {(float)value[0], (float)value[1]};
        return true;
    }
    return false;
}

bool SceneScriptBackend::getBool(uint32_t id, const std::string& property, bool& out) {
    const Layer* layer = layerById(id);
    if (!layer) return false;
    if (property == "visible") {
        out = layer->visible;
        return true;
    }
    if (const auto* sound = dynamic_cast<const SoundLayer*>(layer); sound && property == "playing") {
        out = sound->playing();
        return true;
    }
    return false;
}

bool SceneScriptBackend::setBool(uint32_t id, const std::string& property, bool value) {
    if (property != "visible") return false;
    Layer* layer = layerById(id);
    if (!layer) return false;
    layer->setVisible(value);
    return true;
}

bool SceneScriptBackend::layerCommand(uint32_t id, const std::string& command) {
    auto* sound = dynamic_cast<SoundLayer*>(layerById(id));
    if (!sound) return false;
    if (command == "play")
        sound->start();
    else if (command == "stop" || command == "pause")
        sound->stop();
    else
        return false;
    return true;
}

bool SceneScriptBackend::getNumber(uint32_t id, const std::string& property, double& out) {
    Layer* layer = layerById(id);
    if (!layer) return false;
    if (auto* sound = dynamic_cast<SoundLayer*>(layer)) {
        if (property != "volume") return false;
        out = sound->volume();
        return true;
    }
    if (auto* text = dynamic_cast<TextLayer*>(layer))
        if (text->propertyGetNumber(property, out)) return true;
    if (property != "alpha") return false;
    out = layer->tint[3];
    return true;
}

bool SceneScriptBackend::setNumber(uint32_t id, const std::string& property, double value) {
    Layer* layer = layerById(id);
    if (!layer) return false;
    if (auto* sound = dynamic_cast<SoundLayer*>(layer)) {
        if (property != "volume") return false;
        sound->setVolume((float)value);
        return true;
    }
    if (auto* text = dynamic_cast<TextLayer*>(layer))
        if (text->propertySetNumber(property, value)) return true;
    if (property != "alpha") return false;
    const float alpha = std::clamp((float)value, 0.0f, 1.0f);
    if (auto* image = dynamic_cast<ImageLayer*>(layer))
        image->setAlpha(alpha);
    else
        layer->tint[3] = alpha;
    return true;
}

bool SceneScriptBackend::getString(uint32_t id, const std::string& property, std::string& out) {
    auto* text = dynamic_cast<TextLayer*>(layerById(id));
    return text && text->propertyGetString(property, out);
}

bool SceneScriptBackend::setString(uint32_t id, const std::string& property, const std::string& value) {
    auto* text = dynamic_cast<TextLayer*>(layerById(id));
    return text && text->propertySetString(property, value);
}

uint32_t SceneScriptBackend::parentOf(uint32_t id) {
    const SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
    return node ? node->parent_id : 0;
}

std::vector<uint32_t> SceneScriptBackend::childrenOf(uint32_t id) {
    const SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
    return node ? node->children : std::vector<uint32_t>();
}

uint32_t SceneScriptBackend::findLayerByName(const std::string& name) {
    for (const Layer* layer : ctx_.scene.layers)
        if (layer->scene_object_id != 0 && layer->name == name) return layer->scene_object_id;
    return 0;
}

std::vector<uint32_t> SceneScriptBackend::allLayers() {
    std::vector<uint32_t> ids;
    for (const Layer* layer : ctx_.scene.layers)
        if (layer->scene_object_id != 0) ids.push_back(layer->scene_object_id);
    return ids;
}

ImageLayer* SceneScriptBackend::imageById(uint32_t id) const {
    return dynamic_cast<ImageLayer*>(layerById(id));
}

uint32_t SceneScriptBackend::targetHandle(const AnimationTarget& wanted) {
    for (size_t i = 0; i < targets_.size(); ++i) {
        const AnimationTarget& existing = targets_[i];
        if (existing.sprite == wanted.sprite && existing.layer_id == wanted.layer_id &&
            (wanted.sprite || existing.index == wanted.index))
            return kTargetBase + (uint32_t)i;
    }
    targets_.push_back(wanted);
    return kTargetBase + (uint32_t)targets_.size() - 1;
}

const SceneScriptBackend::AnimationTarget* SceneScriptBackend::target(uint32_t handle) const {
    if (handle < kTargetBase || handle - kTargetBase >= targets_.size()) return nullptr;
    return &targets_[handle - kTargetBase];
}

int SceneScriptBackend::animationLayerCount(uint32_t layer_id) {
    ImageLayer* image = imageById(layer_id);
    return image ? (int)image->puppetLayerCount() : 0;
}

uint32_t SceneScriptBackend::findAnimation(uint32_t layer_id, const std::string& kind, const std::string& key) {
    ImageLayer* image = imageById(layer_id);
    const bool want_any = kind == "any";

    if (kind == "timeline" || want_any) {
        if (const uint32_t handle = animations_.find(layer_id, key)) return handle;
        if (kind == "timeline") return 0;
    }
    if ((kind == "layer" || want_any) && image) {
        int index = key.empty() ? -1 : image->puppetLayerIndex(key);
        if (index < 0 && kind == "layer" && !key.empty() && std::all_of(key.begin(), key.end(), ::isdigit))
            index = std::stoi(key);
        if (index >= 0 && (size_t)index < image->puppetLayerCount())
            return targetHandle({false, layer_id, (size_t)index});
        if (kind == "layer") return 0;
    }
    if ((kind == "texture" || want_any) && image && image->hasSpriteAnimation())
        return targetHandle({true, layer_id, 0});
    return 0;
}

bool SceneScriptBackend::animationGet(uint32_t handle, const std::string& field, double& out) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.get(handle, field, out);
    ImageLayer* image = imageById(t->layer_id);
    if (!image) return false;
    return t->sprite ? image->spriteGet(field, ctx_.time, out) : image->puppetLayerGet(t->index, field, out);
}

bool SceneScriptBackend::animationGetString(uint32_t handle, const std::string& field, std::string& out) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.getString(handle, field, out);
    ImageLayer* image = imageById(t->layer_id);
    if (!image || t->sprite) return false;
    return image->puppetLayerGetString(t->index, field, out);
}

bool SceneScriptBackend::animationSet(uint32_t handle, const std::string& field, double value) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.set(handle, field, value);
    ImageLayer* image = imageById(t->layer_id);
    if (!image) return false;
    return t->sprite ? image->spriteSet(field, value, ctx_.time) : image->puppetLayerSet(t->index, field, value);
}

bool SceneScriptBackend::animationCommand(uint32_t handle, const std::string& command) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.command(handle, command);
    ImageLayer* image = imageById(t->layer_id);
    if (!image) return false;
    return t->sprite ? image->spriteCommand(command, ctx_.time) : image->puppetLayerCommand(t->index, command);
}

std::vector<uint32_t> SceneScriptBackend::takeEndedAnimations() {
    std::vector<uint32_t> ended = animations_.takeEnded();
    // Puppet one-shot clips: only report layers a script already holds a handle for.
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (targets_[i].sprite) continue;
        ImageLayer* image = imageById(targets_[i].layer_id);
        if (image && image->puppetLayerTakeEnded(targets_[i].index)) ended.push_back(kTargetBase + (uint32_t)i);
    }
    return ended;
}
