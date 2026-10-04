#ifndef SCENE_SCRIPTS_H
#define SCENE_SCRIPTS_H

#include <stdint.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "script_scene_backend.h"
#include "shared/core/engine_context.h"
#include "wallpaper/2d/animation/scene_animations.h"
#include "wallpaper/2d/input/pointer_input.h"
#include "wallpaper/2d/script/scene_script.h"

// Transforms live on the SceneTree nodes because that is what the renderer draws from.
class SceneScriptBackend : public ScriptSceneBackend {
   public:
    SceneScriptBackend(EngineContext& ctx, SceneAnimations& animations) : ctx_(ctx), animations_(animations) {}

    bool layerExists(uint32_t id) override;
    std::string layerName(uint32_t id) override;
    bool getVector(uint32_t id, const std::string& property, double out[3], int& components) override;
    bool setVector(uint32_t id, const std::string& property, const double value[3]) override;
    bool getBool(uint32_t id, const std::string& property, bool& out) override;
    bool setBool(uint32_t id, const std::string& property, bool value) override;
    bool getWorldMatrix(uint32_t id, double out[16]) override;
    bool layerCommand(uint32_t id, const std::string& command) override;
    bool getNumber(uint32_t id, const std::string& property, double& out) override;
    bool setNumber(uint32_t id, const std::string& property, double value) override;
    bool getString(uint32_t id, const std::string& property, std::string& out) override;
    bool setString(uint32_t id, const std::string& property, const std::string& value) override;
    uint32_t parentOf(uint32_t id) override;
    std::vector<uint32_t> childrenOf(uint32_t id) override;
    bool setParent(uint32_t id, uint32_t parent) override;
    uint32_t findLayerByName(const std::string& name) override;
    std::vector<uint32_t> allLayers() override;
    uint32_t findAnimation(uint32_t layer_id, const std::string& kind, const std::string& key) override;
    bool animationGet(uint32_t handle, const std::string& field, double& out) override;
    bool animationGetString(uint32_t handle, const std::string& field, std::string& out) override;
    bool animationSet(uint32_t handle, const std::string& field, double value) override;
    bool animationCommand(uint32_t handle, const std::string& command) override;
    std::vector<uint32_t> takeEndedAnimations() override;
    int animationLayerCount(uint32_t layer_id) override;
    bool getSceneProperty(const std::string& name, std::vector<double>& out) override;
    bool setSceneProperty(const std::string& name, const std::vector<double>& value) override;
    uint32_t createLayer(const std::string& config_json) override;
    bool destroyLayer(uint32_t id) override;
    bool sortLayer(uint32_t id, int index) override;
    // Object ids whose destruction was requested since the last call.
    std::vector<uint32_t> takeDestroyed();
    // Called with each object created by a script so its own scripts and animations get bound.
    void setCreatedHandler(std::function<void(const wallpaper_engine::SceneObjectDocument&)> handler) {
        created_handler_ = std::move(handler);
    }
    int effectCount(uint32_t layer_id) override;
    int findEffect(uint32_t layer_id, const std::string& name) override;
    std::string effectName(uint32_t layer_id, int effect) override;
    bool effectVisible(uint32_t layer_id, int effect, bool& out) override;
    bool setEffectVisible(uint32_t layer_id, int effect, bool value) override;
    bool getMaterialProperty(uint32_t layer_id, int effect, const std::string& name, std::vector<double>& out) override;
    bool setMaterialProperty(uint32_t layer_id, int effect, const std::string& name,
                             const std::vector<double>& value) override;

   private:
    class Effect* effectAt(uint32_t layer_id, int effect) const;
    struct AnimationTarget {
        bool sprite = false;  // otherwise a puppet animation layer
        uint32_t layer_id = 0;
        size_t index = 0;
    };
    static constexpr uint32_t kTargetBase = 0x40000000u;

    Layer* layerById(uint32_t id) const;
    uint32_t targetHandle(const AnimationTarget& target);
    const AnimationTarget* target(uint32_t handle) const;
    class ImageLayer* imageById(uint32_t id) const;

    EngineContext& ctx_;
    SceneAnimations& animations_;
    std::vector<AnimationTarget> targets_;
    std::vector<uint32_t> destroyed_;
    uint32_t next_object_id_ = 0;
    std::function<void(const wallpaper_engine::SceneObjectDocument&)> created_handler_;
};

enum class BoundProperty { Origin, Scale, Angles, Visible, Color, Size, EffectVisible, EffectConstant };

// The scene's script-driven properties: each frame the script gets the property's current value and its result is
// written back. Owns the scene backend and keeps it registered with the script engine.
class ScriptBindings {
   public:
    explicit ScriptBindings(EngineContext& ctx);
    ~ScriptBindings();
    ScriptBindings(const ScriptBindings&) = delete;
    ScriptBindings& operator=(const ScriptBindings&) = delete;

    // Binds every script and animation an object declares.
    void addObject(const wallpaper_engine::SceneObjectDocument& object);
    // `effect_index` and `constant` address effect properties (the effect's index on the object; the material key).
    bool add(uint32_t object_id, BoundProperty property, const std::string& script, const std::string& properties_json,
             int effect_index = -1, const std::string& constant = "");
    void update(float dt);
    // Publishes engine.userProperties and sends applyUserProperties once, after the scripts' init().
    void setUserProperties(const UserProperties& properties);
    size_t size() const {
        return bindings_.size();
    }
    SceneAnimations& animations() {
        return animations_;
    }

   private:
    struct Binding {
        uint32_t object_id = 0;
        BoundProperty property = BoundProperty::Origin;
        int effect_index = -1;
        std::string constant;
        std::unique_ptr<SceneScript> script;
        Layer* layer = nullptr;
        bool started = false;
    };

    bool read(const Binding& binding, ScriptValue& value);
    void write(const Binding& binding, const ScriptValue& value);
    void dispatchPointer();
    void removeDestroyed();

    EngineContext& ctx_;
    SceneAnimations animations_;
    SceneScriptBackend backend_;
    std::vector<Binding> bindings_;
    std::vector<Binding> pending_bindings_;
    bool updating_ = false;
    float view_width_ = 0.0f;
    float view_height_ = 0.0f;
    PointerTracker pointer_;
    std::vector<uint32_t> cursor_layers_;  // scene objects with a script that handles any cursor event
    bool cursor_layers_ready_ = false;
    ScriptEvent user_properties_;
    bool user_properties_pending_ = false;
};

#endif  // SCENE_SCRIPTS_H
