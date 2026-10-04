#include "scene_scripts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include "script_engine.h"
#include "shared/assets/media/video_rate.h"
#include "shared/assets/media/video_texture.h"
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
    if (field == "colorn") return &ps.override_color[0];  // one factor, applied to all three components
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
    if (const auto* text = dynamic_cast<const TextLayer*>(layerById(id)); text && property == "backgroundcolor") {
        components = 3;
        return text->propertyGetVector(property, out);
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
    if (auto* text = dynamic_cast<TextLayer*>(layerById(id)); text && property == "backgroundcolor")
        return text->propertySetVector(property, value);
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
    if (const auto* text = dynamic_cast<const TextLayer*>(layer); text && text->propertyGetBool(property, out))
        return true;
    if (property == "particle.playing") {
        const auto* particles = dynamic_cast<const ParticleLayer*>(layer);
        if (!particles || !particles->ps) return false;
        out = (particles->ps->emitting && !particles->ps->paused) || !particles->ps->particles.empty();
        return true;
    }
    if (const auto* image = dynamic_cast<const ImageLayer*>(layer);
        image && (property == "solid" || (image->boneCount() && (property == "rootmotion" || property == "perspective")))) {
        if (property == "solid") {
            out = image->cursor_solid;
            return true;
        }
        out = property == "rootmotion" ? image->rootMotion() : image->perspective;
        return true;
    }
    if (const auto* sound = dynamic_cast<const SoundLayer*>(layer); sound && property == "playing") {
        out = sound->playing();
        return true;
    }
    return false;
}

bool SceneScriptBackend::setBool(uint32_t id, const std::string& property, bool value) {
    if (auto* text = dynamic_cast<TextLayer*>(layerById(id)); text && text->propertySetBool(property, value))
        return true;
    if (property == "solid") {
        ImageLayer* image = imageById(id);
        if (!image) return false;
        image->cursor_solid = value;
        return true;
    }
    if (property == "rootmotion" || property == "perspective") {
        ImageLayer* image = imageById(id);
        if (!image || !image->boneCount()) return false;
        if (property == "rootmotion")
            image->setRootMotion(value);
        else
            image->perspective = value;
        return true;
    }
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

int SceneScriptBackend::effectPassCount(uint32_t layer_id, int effect) {
    const Effect* e = effectAt(layer_id, effect);
    return e ? (int)e->passes.size() : 0;
}

bool SceneScriptBackend::getPassMaterialProperty(uint32_t layer_id, int effect, int pass, const std::string& name,
                                                 std::vector<double>& out) {
    const Effect* e = effectAt(layer_id, effect);
    if (!e || pass < 0 || (size_t)pass >= e->passes.size() || !e->passes[(size_t)pass]) return false;
    const std::vector<float>* values = e->passes[(size_t)pass]->materialConstant(name);
    if (!values) return false;
    out.assign(values->begin(), values->end());
    return true;
}

bool SceneScriptBackend::setPassMaterialProperty(uint32_t layer_id, int effect, int pass, const std::string& name,
                                                 const std::vector<double>& value) {
    Effect* e = effectAt(layer_id, effect);
    if (!e || pass < 0 || (size_t)pass >= e->passes.size() || !e->passes[(size_t)pass]) return false;
    return e->passes[(size_t)pass]->setMaterialConstant(name, std::vector<float>(value.begin(), value.end()));
}

bool SceneScriptBackend::executeMaterialFunction(uint32_t layer_id, int effect, const std::string& name) {
    const Effect* e = effectAt(layer_id, effect);
    auto* image = dynamic_cast<ImageLayer*>(layerById(layer_id));
    if (!e || !image) return false;
    const auto function = e->functions.find(name);
    if (function == e->functions.end()) return false;
    image->clearEffectTargets(function->second);
    return true;
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
            ps->clearParticles();
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
        const std::string field_name = property.substr(sizeof(kParticlePrefix) - 1);
        float* field = ps ? particleScalar(*ps, field_name) : nullptr;
        if (!field) return false;
        *field = (float)value;
        if (field_name == "colorn") {
            ps->override_color[1] = ps->override_color[2] = (float)value;
            ps->has_override_color = true;
        }
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

bool SceneScriptBackend::setParent(uint32_t id, uint32_t parent, const std::string& attachment,
                                   bool adjust_transforms) {
    SceneTree* tree = ctx_.scene.scene_tree;
    SceneTreeNode* node = tree ? tree->find(id) : nullptr;
    if (!node || (parent != 0 && !tree->find(parent))) return false;
    for (uint32_t ancestor = parent; ancestor != 0; ancestor = tree->find(ancestor)->parent_id)
        if (ancestor == id) return false;  // would make the layer its own ancestor

    std::string attachment_name = attachment;
    if (!attachment.empty() && std::all_of(attachment.begin(), attachment.end(), ::isdigit)) {
        const ImageLayer* parent_image = imageById(parent);
        const size_t index = std::stoul(attachment);
        if (parent_image && index < parent_image->puppetModel().attachments.size())
            attachment_name = parent_image->puppetModel().attachments[index].name;
    }

    mat4x4 world;
    const bool keep_world = adjust_transforms && tree->worldTransform(id, world);
    node->parent_id = parent;
    node->attachment = attachment_name;
    tree->rebuildHierarchy();
    if (!keep_world) return true;

    // New local transform = inverse(parent * attachment) * world.
    mat4x4 parent_total;
    mat4x4_identity(parent_total);
    if (parent != 0) {
        tree->worldTransform(parent, parent_total);
        const SceneTreeNode* parent_node = tree->find(parent);
        const auto matrix = parent_node ? parent_node->attachment_transforms.find(attachment_name)
                                        : std::unordered_map<std::string, std::array<float, 16>>::const_iterator{};
        if (parent_node && matrix != parent_node->attachment_transforms.end()) {
            mat4x4 attached;
            memcpy(attached, matrix->second.data(), sizeof(mat4x4));
            mat4x4_mul(parent_total, parent_total, attached);
        }
    }
    mat4x4 inverse, local;
    mat4x4_invert(inverse, parent_total);
    mat4x4_mul(local, inverse, world);
    SceneTree::decompose(local, *node);
    return true;
}

bool SceneScriptBackend::rotateObjectSpace(uint32_t id, const double angles[3]) {
    SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(id) : nullptr;
    if (!node) return false;
    const float current[3] = {node->angles[0], node->angles[1], node->angles[2]};
    const float delta[3] = {(float)angles[0], (float)angles[1], (float)angles[2]};
    mat4x4 base, extra, combined;
    SceneTree::rotationFromAngles(current, base);
    SceneTree::rotationFromAngles(delta, extra);
    mat4x4_mul(combined, base, extra);  // local axes: the new rotation is applied first
    float result[3];
    SceneTree::anglesFromRotation(combined, result);
    for (size_t i = 0; i < 3; ++i) node->angles[i] = result[i];
    return true;
}

int SceneScriptBackend::findAttachment(uint32_t layer_id, const std::string& name) {
    const ImageLayer* image = imageById(layer_id);
    if (!image) return -1;
    for (size_t i = 0; i < image->puppetModel().attachments.size(); ++i)
        if (image->puppetModel().attachments[i].name == name) return (int)i;
    return -1;
}

namespace {
// World transform of a puppet attachment, picked by name or index.
bool attachmentWorldMatrix(SceneTree* tree, const ImageLayer* image, uint32_t layer_id, const std::string& key,
                           mat4x4 out) {
    const SceneTreeNode* node = tree ? tree->find(layer_id) : nullptr;
    if (!image || !node) return false;

    std::string name = key;
    if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) {
        const size_t index = std::stoul(key);
        if (index >= image->puppetModel().attachments.size()) return false;
        name = image->puppetModel().attachments[index].name;
    }
    const auto attachment = node->attachment_transforms.find(name);
    mat4x4 world;
    if (attachment == node->attachment_transforms.end() || !tree->worldTransform(layer_id, world)) return false;
    mat4x4 local;
    memcpy(local, attachment->second.data(), sizeof(mat4x4));
    mat4x4_mul(out, world, local);
    return true;
}
}  // namespace

bool SceneScriptBackend::transformAttachmentToTexture(uint32_t layer_id, uint32_t attachment_layer,
                                                      const std::string& key, std::vector<double>& out) {
    const ImageLayer* target_layer = imageById(layer_id);
    SceneTree* tree = ctx_.scene.scene_tree;
    mat4x4 attached, target_world;
    if (!target_layer || !tree || target_layer->size[0] <= 0.0f || target_layer->size[1] <= 0.0f ||
        !attachmentWorldMatrix(tree, imageById(attachment_layer), attachment_layer, key, attached) ||
        !tree->worldTransform(layer_id, target_world))
        return false;

    // Layer space is centered with y up; texture space runs 0..1 from the top-left corner.
    mat4x4 to_texture, inverse, in_layer, in_texture;
    mat4x4_identity(to_texture);
    to_texture[0][0] = 1.0f / target_layer->size[0];
    to_texture[1][1] = -1.0f / target_layer->size[1];
    to_texture[3][0] = 0.5f;
    to_texture[3][1] = 0.5f;
    mat4x4_invert(inverse, target_world);
    mat4x4_mul(in_layer, inverse, attached);
    mat4x4_mul(in_texture, to_texture, in_layer);
    out = {in_texture[0][0], in_texture[0][1], 0.0, in_texture[1][0], in_texture[1][1],
           0.0,              in_texture[3][0], in_texture[3][1], 1.0};
    return true;
}

bool SceneScriptBackend::getAttachment(uint32_t layer_id, const std::string& key, const std::string& field,
                                       std::vector<double>& out) {
    mat4x4 combined;
    if (!attachmentWorldMatrix(ctx_.scene.scene_tree, imageById(layer_id), layer_id, key, combined)) return false;

    if (field == "matrix") {
        out.resize(16);
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) out[(size_t)(column * 4 + row)] = combined[column][row];
    } else if (field == "origin") {
        out = {combined[3][0], combined[3][1], combined[3][2]};
    } else if (field == "angles") {
        SceneTreeNode scratch;
        SceneTree::decompose(combined, scratch);
        out = {scratch.angles[0], scratch.angles[1], scratch.angles[2]};
    } else {
        return false;
    }
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
        if (existing.sprite == wanted.sprite && existing.video == wanted.video &&
            existing.layer_id == wanted.layer_id && (wanted.sprite || wanted.video || existing.index == wanted.index))
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

uint32_t SceneScriptBackend::createAnimationLayer(uint32_t layer_id, const std::string& config_json) {
    ImageLayer* image = imageById(layer_id);
    cJSON* config = cJSON_Parse(config_json.c_str());
    if (!image || !config) {
        cJSON_Delete(config);
        return 0;
    }
    const auto number = [&](const char* key, double fallback) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(config, key);
        return cJSON_IsNumber(item) ? item->valuedouble : fallback;
    };
    const auto flag = [&](const char* key) { return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(config, key)); };
    std::string animation, name;
    const cJSON* clip = cJSON_GetObjectItemCaseSensitive(config, "animation");
    if (cJSON_IsString(clip) && clip->valuestring)
        animation = clip->valuestring;
    else if (cJSON_IsNumber(clip))
        animation = std::to_string((long)clip->valuedouble);
    const cJSON* label = cJSON_GetObjectItemCaseSensitive(config, "name");
    if (cJSON_IsString(label) && label->valuestring) name = label->valuestring;

    const int index = image->puppetLayerCreate(animation, number("rate", 1.0), number("blend", 1.0), flag("additive"),
                                               flag("once"), flag("autoRemove"), name);
    cJSON_Delete(config);
    return index < 0 ? 0 : targetHandle({false, layer_id, (size_t)index});
}

bool SceneScriptBackend::destroyAnimationLayer(uint32_t layer_id, const std::string& key) {
    ImageLayer* image = imageById(layer_id);
    if (!image) return false;
    int index = image->puppetLayerIndex(key);
    if (index < 0 && !key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) index = std::stoi(key);
    return index >= 0 && image->puppetLayerDestroy((size_t)index);
}

uint32_t SceneScriptBackend::findAnimation(uint32_t layer_id, const std::string& kind, const std::string& key) {
    ImageLayer* image = imageById(layer_id);
    if (kind == "video") {
        if (!image || !image->bound_video_decoder) return 0;
        AnimationTarget video_target;
        video_target.video = true;
        video_target.layer_id = layer_id;
        video_target.seen_loop = image->bound_video_decoder->loopCount();
        return targetHandle(video_target);
    }
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

bool SceneScriptBackend::videoGet(ImageLayer& image, const std::string& field, double& out) {
    wallpaper_engine::VideoTexture* decoder = image.bound_video_decoder;
    auto* active = decoder ? ctx_.asset_mgr->findVideoTexture(decoder) : nullptr;
    if (!decoder || !active) return false;
    if (field == "duration")
        out = decoder->duration();
    else if (field == "rate")
        out = active->rate;
    else if (field == "loop")
        out = decoder->looping() ? 1.0 : 0.0;
    else if (field == "currentTime")
        out = active->position;
    else if (field == "playing")
        out = decoder->isPlaying() ? 1.0 : 0.0;
    else
        return false;
    return true;
}

bool SceneScriptBackend::videoSet(ImageLayer& image, const std::string& field, double value) {
    wallpaper_engine::VideoTexture* decoder = image.bound_video_decoder;
    auto* active = decoder ? ctx_.asset_mgr->findVideoTexture(decoder) : nullptr;
    if (!decoder || !active) return false;
    if (field == "rate") {
        active->rate = clampPlaybackRate((float)value);
    } else if (field == "loop") {
        decoder->setLooping(value != 0.0);
    } else if (field == "currentTime") {
        // The decoder can only restart from the first frame.
        if (value > 0.0) return false;
        decoder->rewind();
        active->position = 0.0;
    } else {
        return false;
    }
    return true;
}

bool SceneScriptBackend::videoCommand(ImageLayer& image, const std::string& command) {
    wallpaper_engine::VideoTexture* decoder = image.bound_video_decoder;
    auto* active = decoder ? ctx_.asset_mgr->findVideoTexture(decoder) : nullptr;
    if (!decoder || !active) return false;
    if (command == "play") {
        if (decoder->isPaused())
            image.resume();
        else
            image.start();
    } else if (command == "pause") {
        image.pause();
    } else if (command == "stop") {
        image.stop();
        decoder->rewind();
        active->position = 0.0;
    } else {
        return false;
    }
    return true;
}

bool SceneScriptBackend::animationGet(uint32_t handle, const std::string& field, double& out) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.get(handle, field, out);
    ImageLayer* image = imageById(t->layer_id);
    if (!image) return false;
    if (t->video) return videoGet(*image, field, out);
    return t->sprite ? image->spriteGet(field, ctx_.time, out) : image->puppetLayerGet(t->index, field, out);
}

bool SceneScriptBackend::animationGetString(uint32_t handle, const std::string& field, std::string& out) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.getString(handle, field, out);
    ImageLayer* image = imageById(t->layer_id);
    if (!image || t->sprite || t->video) return false;
    return image->puppetLayerGetString(t->index, field, out);
}

bool SceneScriptBackend::animationSet(uint32_t handle, const std::string& field, double value) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.set(handle, field, value);
    ImageLayer* image = imageById(t->layer_id);
    if (!image) return false;
    if (t->video) return videoSet(*image, field, value);
    return t->sprite ? image->spriteSet(field, value, ctx_.time) : image->puppetLayerSet(t->index, field, value);
}

bool SceneScriptBackend::animationCommand(uint32_t handle, const std::string& command) {
    const AnimationTarget* t = target(handle);
    if (!t) return animations_.command(handle, command);
    ImageLayer* image = imageById(t->layer_id);
    if (!image) return false;
    if (t->video) return videoCommand(*image, command);
    return t->sprite ? image->spriteCommand(command, ctx_.time) : image->puppetLayerCommand(t->index, command);
}

std::vector<uint32_t> SceneScriptBackend::takeEndedAnimations() {
    std::vector<uint32_t> ended = animations_.takeEnded();
    // Puppet one-shot clips: only report layers a script already holds a handle for.
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (targets_[i].sprite) continue;
        ImageLayer* image = imageById(targets_[i].layer_id);
        if (!image) continue;
        if (targets_[i].video) {
            // A video ends each time the decoder reaches the end of the file.
            const uint32_t passes = image->bound_video_decoder ? image->bound_video_decoder->loopCount() : 0;
            if (passes != targets_[i].seen_loop) {
                targets_[i].seen_loop = passes;
                ended.push_back(kTargetBase + (uint32_t)i);
            }
        } else if (image->puppetLayerTakeEnded(targets_[i].index)) {
            ended.push_back(kTargetBase + (uint32_t)i);
        }
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
    backend_.rememberConfig(id, object.raw_json);
    const auto bind =[&](const wallpaper_engine::ScriptedValue& scripted, BoundProperty property) {
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
        for (const auto& constant : effect.constant_scripts) {
            if (constant.has_animation) {
                // The timeline drives the constant from now on; the key matches the script's `effect:<i>:<name>`.
                wallpaper_engine::PropertyAnimationDocument animation = constant.animation;
                animation.property = "effect:" + std::to_string(i) + ":" + constant.name;
                animations_.add(id, {animation});
            }
            if (!constant.script.empty())
                add(id, BoundProperty::EffectConstant, constant.script.script, constant.script.properties_json,
                    (int)i, constant.name);
        }
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
    if (name == "clearenabled") return boolean(&general.clear_enabled);
    if (name == "camerafade") return boolean(&general.camera_fade);
    if (name == "fov") return numbers({&general.fov});
    if (name == "nearz") return numbers({&general.near_z});
    if (name == "farz") return numbers({&general.far_z});
    if (name == "bloomstrength") return numbers({&general.bloom.strength, &general.bloom.hdr_strength});
    if (name == "bloomthreshold") return numbers({&general.bloom.threshold, &general.bloom.hdr_threshold});
    if (name == "clearcolor") return numbers({&general.clear_color[0], &general.clear_color[1], &general.clear_color[2]});
    if (name == "ambientcolor")
        return numbers({&general.ambient_color[0], &general.ambient_color[1], &general.ambient_color[2]});
    if (name == "skylightcolor")
        return numbers({&general.skylight_color[0], &general.skylight_color[1], &general.skylight_color[2]});
    auto& camera = ctx.scene.camera;
    if (name == "camerazoom") return numbers({&general.zoom});
    if (name == "cameraeye") return numbers({&camera.eye[0], &camera.eye[1], &camera.eye[2]});
    if (name == "cameracenter") return numbers({&camera.center[0], &camera.center[1], &camera.center[2]});
    if (name == "cameraup") return numbers({&camera.up[0], &camera.up[1], &camera.up[2]});
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

std::string SceneScriptBackend::initialLayerConfig(uint32_t id) {
    const auto it = initial_configs_.find(id);
    return it == initial_configs_.end() ? "" : it->second;
}

int SceneScriptBackend::boneCount(uint32_t layer_id) {
    const ImageLayer* image = imageById(layer_id);
    return image ? (int)image->boneCount() : 0;
}

int SceneScriptBackend::findBone(uint32_t layer_id, const std::string& name) {
    const ImageLayer* image = imageById(layer_id);
    return image ? image->boneIndex(name) : -1;
}

std::string SceneScriptBackend::boneName(uint32_t layer_id, int bone) {
    const ImageLayer* image = imageById(layer_id);
    return image && bone >= 0 ? image->boneName((size_t)bone) : "";
}

int SceneScriptBackend::boneParent(uint32_t layer_id, int bone) {
    const ImageLayer* image = imageById(layer_id);
    return image && bone >= 0 ? image->boneParent((size_t)bone) : -1;
}

bool SceneScriptBackend::getBone(uint32_t layer_id, int bone, const std::string& field, std::vector<double>& out) {
    const ImageLayer* image = imageById(layer_id);
    if (!image || bone < 0 || !image->boneGet((size_t)bone, field, out)) return false;
    mat4x4 world;
    if (field == "matrix" && out.size() == 16 && ctx_.scene.scene_tree &&
        ctx_.scene.scene_tree->worldTransform(layer_id, world)) {
        // Bones are kept in the layer's space; scripts see them in the world.
        mat4x4 model, combined;
        for (int i = 0; i < 16; ++i) model[i / 4][i % 4] = (float)out[(size_t)i];
        mat4x4_mul(combined, world, model);
        for (int i = 0; i < 16; ++i) out[(size_t)i] = combined[i / 4][i % 4];
    }
    return true;
}

bool SceneScriptBackend::setBone(uint32_t layer_id, int bone, const std::string& field,
                                 const std::vector<double>& value) {
    ImageLayer* image = imageById(layer_id);
    if (!image || bone < 0) return false;
    mat4x4 world;
    if (field == "matrix" && value.size() >= 16 && ctx_.scene.scene_tree &&
        ctx_.scene.scene_tree->worldTransform(layer_id, world)) {
        mat4x4 inverse, wanted, model;
        mat4x4_invert(inverse, world);
        for (int i = 0; i < 16; ++i) wanted[i / 4][i % 4] = (float)value[(size_t)i];
        mat4x4_mul(model, inverse, wanted);
        std::vector<double> converted(16);
        for (int i = 0; i < 16; ++i) converted[(size_t)i] = model[i / 4][i % 4];
        return image->boneSet((size_t)bone, field, converted);
    }
    return image->boneSet((size_t)bone, field, value);
}

bool SceneScriptBackend::resetBone(uint32_t layer_id, int bone) {
    ImageLayer* image = imageById(layer_id);
    if (!image || bone < 0 || (size_t)bone >= image->boneCount()) return false;
    image->boneReset((size_t)bone);
    return true;
}

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
// The UI language as Wallpaper Engine reports it ("en-us"), taken from the locale.
std::string systemLanguage() {
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable);
        if (!value || !*value) continue;
        std::string language = value;
        language = language.substr(0, language.find_first_of(".@"));
        std::replace(language.begin(), language.end(), '_', '-');
        std::transform(language.begin(), language.end(), language.begin(), ::tolower);
        if (language != "c" && language != "posix") return language;
    }
    return "en-us";
}

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
    if (!general_settings_sent_) {
        general_settings_sent_ = true;
        ScriptEngine::instance().broadcast("applyGeneralSettings",
                                           {{"language", ScriptValue::makeString(systemLanguage())}});
    }
}
