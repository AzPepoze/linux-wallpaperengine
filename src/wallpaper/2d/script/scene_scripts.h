#ifndef SCENE_SCRIPTS_H
#define SCENE_SCRIPTS_H

#include <stdint.h>

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
    uint32_t findLayerByName(const std::string& name) override;
    std::vector<uint32_t> allLayers() override;
    uint32_t findAnimation(uint32_t layer_id, const std::string& kind, const std::string& key) override;
    bool animationGet(uint32_t handle, const std::string& field, double& out) override;
    bool animationGetString(uint32_t handle, const std::string& field, std::string& out) override;
    bool animationSet(uint32_t handle, const std::string& field, double value) override;
    bool animationCommand(uint32_t handle, const std::string& command) override;
    std::vector<uint32_t> takeEndedAnimations() override;
    int animationLayerCount(uint32_t layer_id) override;

   private:
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
};

enum class BoundProperty { Origin, Scale, Angles, Visible, Color };

// The scene's script-driven properties: each frame the script gets the property's current value and its result is
// written back. Owns the scene backend and keeps it registered with the script engine.
class ScriptBindings {
   public:
    explicit ScriptBindings(EngineContext& ctx);
    ~ScriptBindings();
    ScriptBindings(const ScriptBindings&) = delete;
    ScriptBindings& operator=(const ScriptBindings&) = delete;

    bool add(uint32_t object_id, BoundProperty property, const std::string& script, const std::string& properties_json);
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
        std::unique_ptr<SceneScript> script;
        Layer* layer = nullptr;
        bool started = false;
    };

    bool read(const Binding& binding, ScriptValue& value) const;
    void write(const Binding& binding, const ScriptValue& value) const;
    void dispatchPointer();

    EngineContext& ctx_;
    SceneAnimations animations_;
    SceneScriptBackend backend_;
    std::vector<Binding> bindings_;
    PointerTracker pointer_;
    std::vector<uint32_t> cursor_layers_;  // scene objects with a script that handles any cursor event
    bool cursor_layers_ready_ = false;
    ScriptEvent user_properties_;
    bool user_properties_pending_ = false;
};

#endif  // SCENE_SCRIPTS_H
