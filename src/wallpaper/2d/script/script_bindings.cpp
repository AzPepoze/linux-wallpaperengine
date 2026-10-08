#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include "scene_scripts.h"
#include "script_engine.h"
#include "shared/assets/media/video_audio.h"
#include "shared/assets/media/video_rate.h"
#include "shared/assets/media/video_texture.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/model/model_layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/layers/sound/sound_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/scene_builder.h"
#include "wallpaper/2d/tree/scene_tree.h"
#include "wallpaper/2d/tree/scene_visibility.h"

#define TAG "SCRIPT"

ScriptBindings::ScriptBindings(EngineContext& ctx) : ctx_(ctx), animations_(ctx), backend_(ctx, animations_) {
    const std::string storage_root =
        vfs::isVirtual((std::string(ctx.asset_root) + "/").c_str()) ? vfs::sourceDirectory() : ctx.asset_root;
    ScriptEngine::instance().registerScope(this, &backend_, std::filesystem::path(storage_root).filename().string());
    ScriptEngine::instance().setCreationScope(this);
    backend_.setCreatedHandler([this](const wallpaper_engine::SceneObjectDocument& object) { addObject(object); });
}

void ScriptBindings::addObject(const wallpaper_engine::SceneObjectDocument& object) {
    if (!object.node.valid) return;
    const uint32_t id = object.node.id;
    backend_.rememberConfig(id, object.raw_json);
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
    if (!object.text.script.empty())
        add(id, BoundProperty::Text, object.text.script, object.text.script_properties_json);
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
                add(id, BoundProperty::EffectConstant, constant.script.script, constant.script.properties_json, (int)i,
                    constant.name);
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
    if (name == "clearcolor")
        return numbers({&general.clear_color[0], &general.clear_color[1], &general.clear_color[2]});
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
        // A bare string names an asset; the layer type follows from where it lives.
        const std::string path = config->valuestring;
        const auto ends_with = [&](const char* suffix) {
            const size_t length = strlen(suffix);
            return path.size() >= length && path.compare(path.size() - length, length, suffix) == 0;
        };
        const char* key = "image";
        if (path.rfind("particles/", 0) == 0)
            key = "particle";
        else if (ends_with(".mp3") || ends_with(".ogg") || ends_with(".wav"))
            key = "sound";
        cJSON* wrapped = cJSON_CreateObject();
        if (strcmp(key, "sound") == 0) {
            cJSON* sounds = cJSON_AddArrayToObject(wrapped, "sound");
            cJSON_AddItemToArray(sounds, cJSON_CreateString(path.c_str()));
        } else {
            cJSON_AddStringToObject(wrapped, key, path.c_str());
        }
        cJSON_Delete(config);
        config = wrapped;
    }
    std::shared_ptr<ModelData> model;
    if (const cJSON* model_id = cJSON_GetObjectItemCaseSensitive(config, "model"); cJSON_IsNumber(model_id)) {
        const auto found = models_.find((uint32_t)model_id->valuedouble);
        if (found == models_.end()) {
            cJSON_Delete(config);
            return 0;
        }
        model = found->second;
        cJSON_DeleteItemFromObjectCaseSensitive(config, "model");
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

    Layer* layer = nullptr;
    if (model) {
        layer = new ModelLayer(object.name.empty() ? "Model" : object.name.c_str(), model);
        layer->initFromDocument(object, ctx_);
    } else {
        layer = SceneBuilder::buildLayer(object, ctx_);
    }
    if (!layer) return 0;
    ctx_.scene.scene_tree->addNode(SceneBuilder::treeNode(object));
    ctx_.scene.scene_tree->rebuildHierarchy();
    ctx_.scene.layers.push_back(layer);
    if (created_handler_) created_handler_(object);
    return id;
}

uint32_t SceneScriptBackend::createModelData(const std::vector<ShapePatch>& shapes) {
    auto model = std::make_shared<ModelData>();
    for (const ShapePatch& patch : shapes) {
        ModelShape shape;
        applyShapePatch(shape, patch, true);
        model->shapes.push_back(std::move(shape));
    }
    const uint32_t id = ++next_model_id_;
    models_[id] = std::move(model);
    return id;
}

bool SceneScriptBackend::updateModelData(uint32_t model_id, const std::vector<ShapePatch>& shapes, bool replace) {
    const auto found = models_.find(model_id);
    if (found == models_.end()) return false;
    ModelData& model = *found->second;
    std::vector<size_t> removed;
    for (const ShapePatch& patch : shapes) {
        if (patch.index < 0) continue;
        const size_t index = (size_t)patch.index;
        if (patch.remove) {
            if (replace && index < model.shapes.size()) removed.push_back(index);
            continue;
        }
        if (index >= model.shapes.size()) {
            // Only replaceData may add a shape, and only next to the existing ones.
            if (!replace || index != model.shapes.size()) continue;
            model.shapes.emplace_back();
        }
        applyShapePatch(model.shapes[index], patch, replace);
    }
    std::sort(removed.rbegin(), removed.rend());
    for (size_t index : removed) model.shapes.erase(model.shapes.begin() + (std::ptrdiff_t)index);
    ++model.revision;
    return true;
}

bool SceneScriptBackend::destroyModelData(uint32_t model_id) {
    // Layers that still draw the model keep their own reference until they are removed.
    return models_.erase(model_id) > 0;
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
    const auto it =
        std::find_if(layers.begin(), layers.end(), [&](const Layer* l) { return l->scene_object_id == id; });
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
        case BoundProperty::Text:
            loaded->setProperty("text");
            break;
        case BoundProperty::SceneZoom:
            loaded->setProperty("zoom");
            break;
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
        case BoundProperty::Text: {
            std::string text;
            if (!backend_.getString(binding.object_id, "text", text)) return false;
            value = ScriptValue::makeString(text);
            return true;
        }
        case BoundProperty::SceneZoom:
            value = ScriptValue::makeNumber(ctx_.scene.general.zoom);
            return true;
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
        case BoundProperty::Text:
            backend_.setString(binding.object_id, "text", value.text);
            return;
        case BoundProperty::SceneZoom:
            if (std::isfinite(value.number) && value.number > 0.0) ctx_.scene.general.zoom = (float)value.number;
            return;
        case BoundProperty::Origin:
        case BoundProperty::Scale:
        case BoundProperty::Angles: {
            if (!node) return;
            std::array<float, 3>& v = binding.property == BoundProperty::Origin  ? node->origin
                                      : binding.property == BoundProperty::Scale ? node->scale
                                                                                 : node->angles;
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite((float)value.vec[i])) return;
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
    std::vector<HitCandidate> candidates;
    if (input.mouse_position_valid && ctx_.scene.scene_tree) {
        const SceneVisibility visibility(ctx_);
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
            candidate.visible = visibility.visible(*layer);
            candidates.push_back(candidate);
        }
        hit = hitTest({input.mouse_world_x, input.mouse_world_y}, candidates);
    }

    const std::optional<WorldPoint> cursor = input.mouse_position_valid
                                                 ? std::optional<WorldPoint>{{input.mouse_world_x, input.mouse_world_y}}
                                                 : std::nullopt;
    for (const PointerEvent& event : pointer_.update(hit, input.buttons, cursor, candidates)) {
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
