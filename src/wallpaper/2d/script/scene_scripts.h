#ifndef SCENE_SCRIPTS_H
#define SCENE_SCRIPTS_H

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "script_scene_backend.h"
#include "shared/core/engine_context.h"
#include "wallpaper/2d/input/pointer_input.h"
#include "wallpaper/2d/script/scene_script.h"

// ScriptSceneBackend over the running 2D scene: layers are looked up by scene object id, transforms live on the
// SceneTree nodes (which is what the renderer draws from).
class SceneScriptBackend : public ScriptSceneBackend {
   public:
    explicit SceneScriptBackend(EngineContext& ctx) : ctx_(ctx) {}

    bool layerExists(uint32_t id) override;
    std::string layerName(uint32_t id) override;
    bool getVector(uint32_t id, const std::string& property, double out[3], int& components) override;
    bool setVector(uint32_t id, const std::string& property, const double value[3]) override;
    bool getBool(uint32_t id, const std::string& property, bool& out) override;
    bool setBool(uint32_t id, const std::string& property, bool value) override;
    uint32_t parentOf(uint32_t id) override;
    std::vector<uint32_t> childrenOf(uint32_t id) override;
    uint32_t findLayerByName(const std::string& name) override;
    std::vector<uint32_t> allLayers() override;

   private:
    Layer* layerById(uint32_t id) const;
    EngineContext& ctx_;
};

enum class BoundProperty { Origin, Scale, Angles, Visible, Color };

// The scene's SceneScript-driven properties. Each frame the script sees the property's current value and its result
// is written back to the scene tree node or the layer. Owns the scene backend and registers it with the script engine
// for as long as it lives.
class ScriptBindings {
   public:
    explicit ScriptBindings(EngineContext& ctx);
    ~ScriptBindings();
    ScriptBindings(const ScriptBindings&) = delete;
    ScriptBindings& operator=(const ScriptBindings&) = delete;

    // Loads `script` for `property` of the object `object_id`; false (and nothing bound) when it does not compile.
    bool add(uint32_t object_id, BoundProperty property, const std::string& script, const std::string& properties_json);
    void update();
    size_t size() const {
        return bindings_.size();
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
    // Publishes the cursor to `input` and turns it into cursorEnter / Leave / Move / Down / Up / Click events for the
    // scripts of the solid layer under it.
    void dispatchPointer();

    EngineContext& ctx_;
    SceneScriptBackend backend_;
    std::vector<Binding> bindings_;
    PointerTracker pointer_;
    std::vector<uint32_t> cursor_layers_;  // scene objects with a script that handles any cursor event
    bool cursor_layers_ready_ = false;
};

#endif  // SCENE_SCRIPTS_H
