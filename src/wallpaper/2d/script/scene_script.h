#ifndef SCENE_SCRIPT_H
#define SCENE_SCRIPT_H

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

// A property value passed to and from a script's init(value) / update(value). Vectors cross as Vec2 / Vec3 objects;
// Json is only an input: nested data parsed into a JS object (events such as applyUserProperties).
struct ScriptValue {
    enum class Kind { Number, Bool, Vec2, Vec3, String, Json };
    Kind kind = Kind::Number;
    double number = 0.0;  // Number, and Bool as 0 / 1
    double vec[3] = {0.0, 0.0, 0.0};
    std::string text;

    static ScriptValue makeNumber(double value);
    static ScriptValue makeBool(bool value);
    static ScriptValue makeVec3(double x, double y, double z);
    static ScriptValue makeString(std::string value);
    static ScriptValue makeVec2(double x, double y);
    static ScriptValue makeJson(std::string json);
};

// The object passed to an event hook (cursorMove, mediaPropertiesChanged...): its named fields in order.
using ScriptEvent = std::vector<std::pair<std::string, ScriptValue>>;

// Minimal Wallpaper Engine SceneScript host backed by QuickJS. Text scripts
// expose `update(value)` and the `createScriptProperties` builder.
class SceneScript {
   public:
    SceneScript();
    ~SceneScript();

    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    // Evaluates an ES module text script and applies the scene's
    // `scriptproperties` overrides. False when empty or invalid.
    bool load(const std::string& source, const std::string& script_properties_json);

    // Calls `update(value)`; stores the result in `out`. False without `update`.
    bool update(const std::string& value, std::string& out);

    // Property scripts (e.g. an image's alpha): `init(value)` gets `value` and may return the starting value, which
    // replaces it; `update(value)` returns the next one. False when the hook is missing or did not return a number.
    // Per-frame inputs (engine.frametime, audio buffers) live on ScriptEngine.
    bool callInit(double& value);
    bool updateNumber(double value, double& out);

    // Typed variants: `value` goes in as the hook's argument and is replaced by its result when the script returned
    // something of a compatible type (a number broadcasts into a vector). Returns false, leaving `value` unchanged,
    // when the hook is missing, threw, or returned undefined / an incompatible value.
    bool initValue(ScriptValue& value);
    bool updateValue(ScriptValue& value);

    // The scene object `thisLayer` refers to while this script runs (0 = none). May be set before or after load().
    void setLayerId(uint32_t layer_id);
    uint32_t layerId() const;

    // Calls the exported event hook `name` with the event object (no argument when `event` is empty and
    // `with_event` is false). True when the script defines the hook and it ran without throwing.
    bool callHook(const char* name, const ScriptEvent& event, bool with_event = true);

    // Calls a single-string-argument hook such as `mediaPropertiesChanged`.
    void callWithString(const std::string& function, const std::string& argument);

    // Calls `mediaPropertiesChanged({ title })` for media-title scripts.
    void mediaPropertiesChanged(const std::string& title);

    bool valid() const;

    // Diagnostics: the most recent script exception ("hook: message"), how many have been seen, and whether the
    // script defines a global hook of that name.
    const std::string& lastError() const;
    const std::string& lastStack() const;
    int errorCount() const;
    bool hasFunction(const char* name) const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // SCENE_SCRIPT_H
