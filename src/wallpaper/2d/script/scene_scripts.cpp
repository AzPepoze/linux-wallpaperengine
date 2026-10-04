#include "scene_scripts.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>

#include "script_engine.h"
#include "shared/core/logger.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/layers/sound/sound_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/scene_builder.h"
#include "wallpaper/2d/tree/scene_tree.h"

#define TAG "SCRIPT"

namespace {
constexpr char kParticlePrefix[] = "particle.";

ParticleSystem* particleSystemOf(Layer* layer) {
    auto* particles = dynamic_cast<ParticleLayer*>(layer);
    return particles ? particles->ps : nullptr;
}

float* particleScalar(ParticleSystem& ps, const std::string& field) {
    if (field == "alpha") return &ps.override_alpha;
    if (field == "size") return &ps.override_size;
    if (field == "count") return &ps.override_count;
    if (field == "speed") return &ps.override_speed;
    if (field == "lifetime") return &ps.override_lifetime;
    if (field == "rate") return &ps.override_rate;
    return nullptr;
}

float* particleVector(ParticleSystem& ps, const std::string& field) {
    if (field == "color") return ps.override_color;
    if (field.size() == 13 && field.compare(0, 12, "controlpoint") == 0 && field[12] >= '0' && field[12] <= '7')
        return ps.control_points[field[12] - '0'];
    return nullptr;
}

bool hasParticlePrefix(const std::string& property) {
    return property.compare(0, sizeof(kParticlePrefix) - 1, kParticlePrefix) == 0;
}
}  // namespace

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
    if (hasParticlePrefix(property)) {
        ParticleSystem* ps = particleSystemOf(layerById(id));
        const float* field = ps ? particleVector(*ps, property.substr(sizeof(kParticlePrefix) - 1)) : nullptr;
        if (!field) return false;
        for (int i = 0; i < 3; ++i) out[i] = field[i];
        components = 3;
        return true;
    }
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
    if (hasParticlePrefix(property)) {
        ParticleSystem* ps = particleSystemOf(layerById(id));
        const std::string field_name = property.substr(sizeof(kParticlePrefix) - 1);
        float* field = ps ? particleVector(*ps, field_name) : nullptr;
        if (!field) return false;
        for (int i = 0; i < 3; ++i) field[i] = (float)value[i];
        if (field_name == "color") ps->has_override_color = true;
        return true;
    }
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
    if (property == "size") {
        Layer* layer = layerById(id);
        if (!layer) return false;
        layer->size[0] = (float)value[0];
        layer->size[1] = (float)value[1];
        return true;
    }
    return false;
}

bool SceneScriptBackend::getBool(uint32_t id, const std::string& property, bool& out) {
    const Layer* layer = layerById(id);
    if (!layer) {
        // Groups have no layer; their visibility lives on the tree node.
        const SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
        if (!node || property != "visible") return false;
        out = node->visible;
        return true;
    }
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
    if (!layer) {
        SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
        if (!node) return false;
        node->visible = value;
        return true;
    }
    layer->setVisible(value);
    return true;
}

Effect* SceneScriptBackend::effectAt(uint32_t layer_id, int effect) const {
    const Layer* layer = layerById(layer_id);
    if (!layer || effect < 0 || (size_t)effect >= layer->effects.size()) return nullptr;
    return layer->effects[(size_t)effect];
}

int SceneScriptBackend::effectCount(uint32_t layer_id) {
    const Layer* layer = layerById(layer_id);
    return layer ? (int)layer->effects.size() : 0;
}

int SceneScriptBackend::findEffect(uint32_t layer_id, const std::string& name) {
    const Layer* layer = layerById(layer_id);
    if (!layer) return -1;
    for (size_t i = 0; i < layer->effects.size(); ++i)
        if (layer->effects[i] && layer->effects[i]->name == name) return (int)i;
    return -1;
}

std::string SceneScriptBackend::effectName(uint32_t layer_id, int effect) {
    const Effect* e = effectAt(layer_id, effect);
    return e ? e->name : "";
}

bool SceneScriptBackend::effectVisible(uint32_t layer_id, int effect, bool& out) {
    const Effect* e = effectAt(layer_id, effect);
    if (!e) return false;
    out = e->visible;
    return true;
}

bool SceneScriptBackend::setEffectVisible(uint32_t layer_id, int effect, bool value) {
    Effect* e = effectAt(layer_id, effect);
    if (!e) return false;
    e->visible = value;
    return true;
}

bool SceneScriptBackend::getMaterialProperty(uint32_t layer_id, int effect, const std::string& name,
                                             std::vector<double>& out) {
    const Effect* e = effectAt(layer_id, effect);
    if (!e) return false;
    for (const ShaderPass* pass : e->passes) {
        if (!pass) continue;
        if (const std::vector<float>* values = pass->materialConstant(name)) {
            out.assign(values->begin(), values->end());
            return true;
        }
    }
    return false;
}

bool SceneScriptBackend::setMaterialProperty(uint32_t layer_id, int effect, const std::string& name,
                                             const std::vector<double>& value) {
    Effect* e = effectAt(layer_id, effect);
    if (!e) return false;
    const std::vector<float> values(value.begin(), value.end());
    bool applied = false;
    for (ShaderPass* pass : e->passes)
        if (pass && pass->setMaterialConstant(name, values)) applied = true;
    return applied;
}

bool SceneScriptBackend::layerCommand(uint32_t id, const std::string& command) {
    if (hasParticlePrefix(command)) {
        ParticleSystem* ps = particleSystemOf(layerById(id));
        if (!ps) return false;
        const std::string name = command.substr(sizeof(kParticlePrefix) - 1);
        if (name == "play") {
            ps->emitting = true;
            ps->paused = false;
        } else if (name == "pause") {
            ps->paused = true;
        } else if (name == "stop") {
            ps->emitting = false;
            ps->paused = false;
        } else if (name.compare(0, 5, "emit:") == 0) {
            ps->emitParticles(std::atoi(name.c_str() + 5));
        } else {
            return false;
        }
        return true;
    }
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
    if (hasParticlePrefix(property)) {
        ParticleSystem* ps = particleSystemOf(layer);
        const float* value = ps ? particleScalar(*ps, property.substr(sizeof(kParticlePrefix) - 1)) : nullptr;
        if (!value) return false;
        out = *value;
        return true;
    }
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
    if (hasParticlePrefix(property)) {
        ParticleSystem* ps = particleSystemOf(layer);
        float* field = ps ? particleScalar(*ps, property.substr(sizeof(kParticlePrefix) - 1)) : nullptr;
        if (!field) return false;
        *field = (float)value;
        return true;
    }
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

bool SceneScriptBackend::setParent(uint32_t id, uint32_t parent) {
    SceneTree* tree = ctx_.scene.scene_tree;
    SceneTreeNode* node = tree ? tree->find(id) : nullptr;
    if (!node || (parent != 0 && !tree->find(parent))) return false;
    for (uint32_t ancestor = parent; ancestor != 0; ancestor = tree->find(ancestor)->parent_id)
        if (ancestor == id) return false;  // would make the layer its own ancestor
    node->parent_id = parent;
    tree->rebuildHierarchy();
    return true;
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

ScriptBindings::ScriptBindings(EngineContext& ctx) : ctx_(ctx), animations_(ctx), backend_(ctx, animations_) {
    ScriptEngine::instance().registerScope(this, &backend_, std::filesystem::path(ctx.asset_root).filename().string());
    ScriptEngine::instance().setCreationScope(this);
    backend_.setCreatedHandler([this](const wallpaper_engine::SceneObjectDocument& object) { addObject(object); });
}

void ScriptBindings::addObject(const wallpaper_engine::SceneObjectDocument& object) {
    if (!object.node.valid) return;
    const uint32_t id = object.node.id;
    const auto bind = [&](const wallpaper_engine::ScriptedValue& scripted, BoundProperty property) {
        if (!scripted.empty()) add(id, property, scripted.script, scripted.properties_json);
    };
    if (!object.animations.empty()) animations_.add(id, object.animations);
    bind(object.node.origin_script, BoundProperty::Origin);
    bind(object.node.scale_script, BoundProperty::Scale);
    bind(object.node.angles_script, BoundProperty::Angles);
    bind(object.visible_script, BoundProperty::Visible);
    bind(object.image.color_script, BoundProperty::Color);
    bind(object.image.size_script, BoundProperty::Size);
    for (size_t i = 0; i < object.effects.size(); ++i) {
        const auto& effect = object.effects[i];
        if (!effect.visible_script.empty())
            add(id, BoundProperty::EffectVisible, effect.visible_script.script, effect.visible_script.properties_json,
                (int)i);
        for (const auto& constant : effect.constant_scripts)
            add(id, BoundProperty::EffectConstant, constant.script.script, constant.script.properties_json, (int)i,
                constant.name);
    }
}

namespace {
struct SceneField {
    std::vector<float*> floats;  // written together (a color, or a setting that has an HDR twin)
    bool* flag = nullptr;
};

bool sceneField(EngineContext& ctx, const std::string& name, SceneField& field) {
    auto& general = ctx.scene.general;
    const auto numbers = [&](std::initializer_list<float*> list) {
        field.floats = list;
        return true;
    };
    const auto boolean = [&](bool* value) {
        field.flag = value;
        return true;
    };
    if (name == "bloom") return boolean(&general.bloom.enabled);
    if (name == "bloomstrength") return numbers({&general.bloom.strength, &general.bloom.hdr_strength});
    if (name == "bloomthreshold") return numbers({&general.bloom.threshold, &general.bloom.hdr_threshold});
    if (name == "clearcolor") return numbers({&general.clear_color[0], &general.clear_color[1], &general.clear_color[2]});
    if (name == "ambientcolor")
        return numbers({&general.ambient_color[0], &general.ambient_color[1], &general.ambient_color[2]});
    if (name == "skylightcolor")
        return numbers({&general.skylight_color[0], &general.skylight_color[1], &general.skylight_color[2]});
    if (name == "cameraparallax") return boolean(&ctx.parallax.enabled);
    if (name == "cameraparallaxamount") return numbers({&ctx.parallax.amount});
    if (name == "cameraparallaxdelay") return numbers({&ctx.parallax.delay});
    if (name == "cameraparallaxmouseinfluence") return numbers({&ctx.parallax.mouse_influence});
    if (name == "camerashake") return boolean(&ctx.shake.enabled);
    if (name == "camerashakeamplitude") return numbers({&ctx.shake.amplitude});
    if (name == "camerashakespeed") return numbers({&ctx.shake.speed});
    if (name == "camerashakeroughness") return numbers({&ctx.shake.roughness});
    return false;
}
}  // namespace

bool SceneScriptBackend::getSceneProperty(const std::string& name, std::vector<double>& out) {
    SceneField field;
    if (!sceneField(ctx_, name, field)) return false;
    out.clear();
    if (field.flag) out.push_back(*field.flag ? 1.0 : 0.0);
    // The first float is the live one for settings with an HDR twin.
    const size_t count = name.rfind("bloom", 0) == 0 ? 1 : field.floats.size();
    for (size_t i = 0; i < count && i < field.floats.size(); ++i) out.push_back(*field.floats[i]);
    return true;
}

bool SceneScriptBackend::setSceneProperty(const std::string& name, const std::vector<double>& value) {
    SceneField field;
    if (!sceneField(ctx_, name, field) || value.empty()) return false;
    if (field.flag) {
        *field.flag = value[0] != 0.0;
        return true;
    }
    if (name.rfind("bloom", 0) == 0) {
        for (float* target : field.floats) *target = (float)value[0];
        return true;
    }
    for (size_t i = 0; i < field.floats.size(); ++i) *field.floats[i] = (float)value[std::min(i, value.size() - 1)];
    return true;
}

uint32_t SceneScriptBackend::createLayer(const std::string& config_json) {
    if (!ctx_.scene.scene_tree) return 0;
    cJSON* config = cJSON_Parse(config_json.c_str());
    if (!config) return 0;
    if (cJSON_IsString(config)) {
        // A bare string names an image asset.
        cJSON* wrapped = cJSON_CreateObject();
        cJSON_AddStringToObject(wrapped, "image", config->valuestring);
        cJSON_Delete(config);
        config = wrapped;
    }
    uint32_t highest = ctx_.scene.scene_tree->maxId();
    highest = std::max(highest, next_object_id_);
    const uint32_t id = highest + 1;
    next_object_id_ = id;
    cJSON_DeleteItemFromObjectCaseSensitive(config, "id");
    cJSON_AddNumberToObject(config, "id", id);
    char* text = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (!text) return 0;

    wallpaper_engine::SceneObjectDocument object;
    const bool parsed = wallpaper_engine::parseSceneObject(text, object);
    cJSON_free(text);
    if (!parsed) return 0;

    Layer* layer = SceneBuilder::buildLayer(object, ctx_);
    if (!layer) return 0;
    ctx_.scene.scene_tree->addNode(SceneBuilder::treeNode(object));
    ctx_.scene.scene_tree->rebuildHierarchy();
    ctx_.scene.layers.push_back(layer);
    if (created_handler_) created_handler_(object);
    return id;
}

bool SceneScriptBackend::destroyLayer(uint32_t id) {
    Layer* layer = layerById(id);
    if (!layer || std::find(destroyed_.begin(), destroyed_.end(), id) != destroyed_.end()) return false;
    layer->setVisible(false);
    destroyed_.push_back(id);
    return true;
}

std::vector<uint32_t> SceneScriptBackend::takeDestroyed() {
    return std::exchange(destroyed_, {});
}

bool SceneScriptBackend::sortLayer(uint32_t id, int index) {
    auto& layers = ctx_.scene.layers;
    const auto it = std::find_if(layers.begin(), layers.end(), [&](const Layer* l) { return l->scene_object_id == id; });
    if (it == layers.end()) return false;
    Layer* layer = *it;
    layers.erase(it);
    const size_t position = (size_t)std::clamp(index, 0, (int)layers.size());
    layers.insert(layers.begin() + (std::ptrdiff_t)position, layer);
    return true;
}

void ScriptBindings::setUserProperties(const UserProperties& properties) {
    user_properties_.clear();
    for (const UserPropertyDef& def : properties.all()) {
        const UserPropertyValue& value = def.value;
        switch (value.type) {
            case UserPropertyValue::Type::Bool:
                user_properties_.emplace_back(def.key, ScriptValue::makeBool(value.b));
                break;
            case UserPropertyValue::Type::Number:
                user_properties_.emplace_back(def.key, ScriptValue::makeNumber(value.n));
                break;
            case UserPropertyValue::Type::Color:
                user_properties_.emplace_back(def.key,
                                              ScriptValue::makeVec3(value.color[0], value.color[1], value.color[2]));
                break;
            case UserPropertyValue::Type::Text:
                user_properties_.emplace_back(def.key, ScriptValue::makeString(value.text));
                break;
        }
    }
    ScriptEngine::instance().setUserProperties(user_properties_);
    user_properties_pending_ = true;
}

ScriptBindings::~ScriptBindings() {
    ScriptEngine::instance().setActiveScope(this);
    ScriptEngine::instance().broadcast("destroy", {});
    bindings_.clear();  // scripts first: they may still call back into the backend while shutting down
    ScriptEngine::instance().unregisterScope(this);
    ScriptEngine::instance().setActiveScope(nullptr);
}

bool ScriptBindings::add(uint32_t object_id, BoundProperty property, const std::string& script,
                         const std::string& properties_json, int effect_index, const std::string& constant) {
    auto loaded = std::make_unique<SceneScript>();
    loaded->setLayerId(object_id);
    switch (property) {
        case BoundProperty::Origin:
            loaded->setProperty("origin");
            break;
        case BoundProperty::Scale:
            loaded->setProperty("scale");
            break;
        case BoundProperty::Angles:
            loaded->setProperty("angles");
            break;
        case BoundProperty::Visible:
            loaded->setProperty("visible");
            break;
        case BoundProperty::Color:
            loaded->setProperty("color");
            break;
        case BoundProperty::Size:
            loaded->setProperty("size");
            break;
        case BoundProperty::EffectVisible:
            // `effect:<index>:<what>` makes thisObject the effect (see the prelude).
            loaded->setProperty("effect:" + std::to_string(effect_index) + ":visible");
            break;
        case BoundProperty::EffectConstant:
            loaded->setProperty("effect:" + std::to_string(effect_index) + ":" + constant);
            break;
    }
    if (!loaded->load(script, properties_json)) {
        LOG_TAG_W(TAG, "object %u: property script failed to load: %s", object_id, loaded->lastError().c_str());
        return false;
    }
    Binding binding;
    binding.object_id = object_id;
    binding.property = property;
    binding.effect_index = effect_index;
    binding.constant = constant;
    binding.script = std::move(loaded);
    // Bindings added by a running script wait until the update loop is done.
    (updating_ ? pending_bindings_ : bindings_).push_back(std::move(binding));
    return true;
}

bool ScriptBindings::read(const Binding& binding, ScriptValue& value) {
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
            if (!binding.layer) {
                if (!node) return false;
                value = ScriptValue::makeBool(node->visible);
                return true;
            }
            value = ScriptValue::makeBool(binding.layer->visible);
            return true;
        case BoundProperty::Color:
            if (!binding.layer) return false;
            value = ScriptValue::makeVec3(binding.layer->tint[0], binding.layer->tint[1], binding.layer->tint[2]);
            return true;
        case BoundProperty::Size:
            if (!binding.layer) return false;
            value = ScriptValue::makeVec2(binding.layer->size[0], binding.layer->size[1]);
            return true;
        case BoundProperty::EffectVisible: {
            bool visible = false;
            if (!backend_.effectVisible(binding.object_id, binding.effect_index, visible)) return false;
            value = ScriptValue::makeBool(visible);
            return true;
        }
        case BoundProperty::EffectConstant: {
            std::vector<double> current;
            if (!backend_.getMaterialProperty(binding.object_id, binding.effect_index, binding.constant, current))
                return false;
            switch (current.size()) {
                case 1:
                    value = ScriptValue::makeNumber(current[0]);
                    return true;
                case 2:
                    value = ScriptValue::makeVec2(current[0], current[1]);
                    return true;
                case 3:
                    value = ScriptValue::makeVec3(current[0], current[1], current[2]);
                    return true;
            }
            return false;
        }
    }
    return false;
}

void ScriptBindings::write(const Binding& binding, const ScriptValue& value) {
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
            if (binding.layer)
                binding.layer->setVisible(value.number != 0.0);
            else if (node)
                node->visible = value.number != 0.0;
            return;
        case BoundProperty::Color:
            if (binding.layer)
                for (int i = 0; i < 3; ++i) binding.layer->tint[i] = (float)value.vec[i];
            return;
        case BoundProperty::Size:
            if (binding.layer) {
                binding.layer->size[0] = (float)value.vec[0];
                binding.layer->size[1] = (float)value.vec[1];
            }
            return;
        case BoundProperty::EffectVisible:
            backend_.setEffectVisible(binding.object_id, binding.effect_index, value.number != 0.0);
            return;
        case BoundProperty::EffectConstant: {
            std::vector<double> next;
            switch (value.kind) {
                case ScriptValue::Kind::Vec2:
                    next = {value.vec[0], value.vec[1]};
                    break;
                case ScriptValue::Kind::Vec3:
                    next = {value.vec[0], value.vec[1], value.vec[2]};
                    break;
                default:
                    next = {value.number};
                    break;
            }
            backend_.setMaterialProperty(binding.object_id, binding.effect_index, binding.constant, next);
            return;
        }
    }
}

namespace {
const std::vector<const char*> kCursorHooks = {"cursorEnter", "cursorLeave", "cursorMove",
                                               "cursorDown",  "cursorUp",    "cursorClick"};

const char* hookFor(PointerEventType type) {
    switch (type) {
        case PointerEventType::Enter:
            return "cursorEnter";
        case PointerEventType::Leave:
            return "cursorLeave";
        case PointerEventType::Move:
            return "cursorMove";
        case PointerEventType::Down:
            return "cursorDown";
        case PointerEventType::Up:
            return "cursorUp";
        case PointerEventType::Click:
            return "cursorClick";
    }
    return "";
}
}  // namespace

void ScriptBindings::dispatchPointer() {
    ScriptEngine& engine = ScriptEngine::instance();
    const InputState& input = ctx_.input;
    engine.setInput(input.mouse_world_x, input.mouse_world_y, input.mouse_x, input.mouse_y, input.left_down());

    if (!cursor_layers_ready_) {
        cursor_layers_ = engine.layersWithHooks(kCursorHooks);
        cursor_layers_ready_ = true;
    }
    if (cursor_layers_.empty()) return;

    std::optional<LocalHit> hit;
    if (input.mouse_position_valid && ctx_.scene.scene_tree) {
        std::vector<HitCandidate> candidates;
        for (Layer* layer : ctx_.scene.layers) {
            auto* image = dynamic_cast<ImageLayer*>(layer);
            if (!image || !image->cursor_solid || layer->scene_object_id == 0) continue;
            if (std::find(cursor_layers_.begin(), cursor_layers_.end(), layer->scene_object_id) == cursor_layers_.end())
                continue;
            mat4x4 world;
            if (!ctx_.scene.scene_tree->worldTransform(layer->scene_object_id, world)) continue;
            HitCandidate candidate;
            candidate.id = layer->scene_object_id;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row) candidate.world[column * 4 + row] = world[column][row];
            candidate.size[0] = image->size[0];
            candidate.size[1] = image->size[1];
            candidate.solid = true;
            candidate.visible = layer->visible;
            candidates.push_back(candidate);
        }
        hit = hitTest({input.mouse_world_x, input.mouse_world_y}, candidates);
    }

    for (const PointerEvent& event : pointer_.update(hit, input.buttons)) {
        // Press, release and click are the primary button's; hover events are the same for every button.
        const bool button_event = event.type == PointerEventType::Down || event.type == PointerEventType::Up ||
                                  event.type == PointerEventType::Click;
        if (button_event && event.button != 0) continue;
        ScriptEvent payload = {
            {"worldPosition", ScriptValue::makeVec3(event.world_x, event.world_y, 0.0)},
            {"localPosition", ScriptValue::makeVec3(event.local_x, event.local_y, 0.0)},
        };
        engine.dispatchToLayer(event.id, hookFor(event.type), payload);
    }
}

void ScriptBindings::removeDestroyed() {
    for (uint32_t id : backend_.takeDestroyed()) {
        bindings_.erase(
            std::remove_if(bindings_.begin(), bindings_.end(), [&](const Binding& b) { return b.object_id == id; }),
            bindings_.end());
        auto& layers = ctx_.scene.layers;
        const auto it =
            std::find_if(layers.begin(), layers.end(), [&](const Layer* l) { return l->scene_object_id == id; });
        if (it != layers.end()) {
            delete *it;
            layers.erase(it);
        }
        if (ctx_.scene.scene_tree) ctx_.scene.scene_tree->removeNode(id);
    }
}

void ScriptBindings::update(float dt) {
    animations_.update(dt);
    const float width = ctx_.renderer.view_width, height = ctx_.renderer.view_height;
    if (width > 0.0f && height > 0.0f && (width != view_width_ || height != view_height_)) {
        const bool first = view_width_ == 0.0f;
        view_width_ = width;
        view_height_ = height;
        if (!first)
            ScriptEngine::instance().broadcast(
                "resizeScreen", {{"x", ScriptValue::makeNumber(width)}, {"y", ScriptValue::makeNumber(height)}});
    }
    for (uint32_t handle : backend_.takeEndedAnimations()) ScriptEngine::instance().animationEnded(handle);
    dispatchPointer();
    updating_ = true;
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
    updating_ = false;
    for (Binding& created : pending_bindings_) bindings_.push_back(std::move(created));
    pending_bindings_.clear();
    removeDestroyed();

    if (user_properties_pending_) {
        user_properties_pending_ = false;
        ScriptEngine::instance().broadcast("applyUserProperties", user_properties_);
    }
}
