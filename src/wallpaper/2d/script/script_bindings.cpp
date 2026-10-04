#include <algorithm>
#include <cctype>
#include <optional>

#include "scene_scripts.h"
#include "script_engine.h"
#include "shared/core/logger.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/sound/sound_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

#define TAG "SCRIPT"

ScriptBindings::ScriptBindings(EngineContext& ctx) : ctx_(ctx), animations_(ctx), backend_(ctx, animations_) {
    ScriptEngine::instance().setSceneBackend(&backend_);
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
    bindings_.clear();  // scripts first: they may still call back into the backend while shutting down
    if (ScriptEngine::instance().sceneBackend() == &backend_) ScriptEngine::instance().setSceneBackend(nullptr);
}

bool ScriptBindings::add(uint32_t object_id, BoundProperty property, const std::string& script,
                         const std::string& properties_json) {
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
    }
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

void ScriptBindings::update(float dt) {
    animations_.update(dt);
    for (uint32_t handle : backend_.takeEndedAnimations()) ScriptEngine::instance().animationEnded(handle);
    dispatchPointer();
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

    if (user_properties_pending_) {
        user_properties_pending_ = false;
        ScriptEngine::instance().broadcast("applyUserProperties", user_properties_);
    }
}
