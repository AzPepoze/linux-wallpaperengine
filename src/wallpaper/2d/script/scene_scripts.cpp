#include "scene_scripts.h"

#include "script_engine.h"
#include "shared/core/logger.h"
#include "wallpaper/2d/layers/layer.h"
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
    return false;
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
    if (property != "visible") return false;
    const Layer* layer = layerById(id);
    if (!layer) return false;
    out = layer->visible;
    return true;
}

bool SceneScriptBackend::setBool(uint32_t id, const std::string& property, bool value) {
    if (property != "visible") return false;
    Layer* layer = layerById(id);
    if (!layer) return false;
    layer->setVisible(value);
    return true;
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

ScriptBindings::ScriptBindings(EngineContext& ctx) : ctx_(ctx), backend_(ctx) {
    ScriptEngine::instance().setSceneBackend(&backend_);
}

ScriptBindings::~ScriptBindings() {
    bindings_.clear();  // scripts first: they may still call back into the backend while shutting down
    if (ScriptEngine::instance().sceneBackend() == &backend_) ScriptEngine::instance().setSceneBackend(nullptr);
}

bool ScriptBindings::add(uint32_t object_id, BoundProperty property, const std::string& script,
                         const std::string& properties_json) {
    auto loaded = std::make_unique<SceneScript>();
    loaded->setLayerId(object_id);
    if (!loaded->load(script, properties_json)) {
        LOG_TAG_W(TAG, "object %u: property script failed to load: %s", object_id, loaded->lastError().c_str());
        return false;
    }
    Binding binding;
    binding.object_id = object_id;
    binding.property = property;
    binding.script = std::move(loaded);
    bindings_.push_back(std::move(binding));
    return true;
}

bool ScriptBindings::read(const Binding& binding, ScriptValue& value) const {
    const SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(binding.object_id) : nullptr;
    switch (binding.property) {
        case BoundProperty::Origin:
        case BoundProperty::Scale:
        case BoundProperty::Angles: {
            if (!node) return false;
            const std::array<float, 3>& v = binding.property == BoundProperty::Origin  ? node->origin
                                            : binding.property == BoundProperty::Scale ? node->scale
                                                                                       : node->angles;
            value = ScriptValue::makeVec3(v[0], v[1], v[2]);
            return true;
        }
        case BoundProperty::Visible:
            if (!binding.layer) return false;
            value = ScriptValue::makeBool(binding.layer->visible);
            return true;
        case BoundProperty::Color:
            if (!binding.layer) return false;
            value = ScriptValue::makeVec3(binding.layer->tint[0], binding.layer->tint[1], binding.layer->tint[2]);
            return true;
    }
    return false;
}

void ScriptBindings::write(const Binding& binding, const ScriptValue& value) const {
    SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(binding.object_id) : nullptr;
    switch (binding.property) {
        case BoundProperty::Origin:
        case BoundProperty::Scale:
        case BoundProperty::Angles: {
            if (!node) return;
            std::array<float, 3>& v = binding.property == BoundProperty::Origin  ? node->origin
                                      : binding.property == BoundProperty::Scale ? node->scale
                                                                                 : node->angles;
            for (int i = 0; i < 3; ++i) v[i] = (float)value.vec[i];
            return;
        }
        case BoundProperty::Visible:
            if (binding.layer) binding.layer->setVisible(value.number != 0.0);
            return;
        case BoundProperty::Color:
            if (binding.layer)
                for (int i = 0; i < 3; ++i) binding.layer->tint[i] = (float)value.vec[i];
            return;
    }
}

void ScriptBindings::update() {
    for (Binding& binding : bindings_) {
        if (!binding.layer) {
            for (Layer* layer : ctx_.scene.layers)
                if (layer->scene_object_id == binding.object_id) binding.layer = layer;
        }
        ScriptValue value;
        if (!read(binding, value)) continue;

        // init(value) runs once, after the whole scene exists, so scripts can look up other layers.
        if (!binding.started) {
            binding.started = true;
            if (binding.script->initValue(value)) write(binding, value);
        }
        if (!read(binding, value)) continue;
        if (binding.script->updateValue(value)) write(binding, value);
    }
}
