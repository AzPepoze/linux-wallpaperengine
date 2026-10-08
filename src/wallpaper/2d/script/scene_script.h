#ifndef SCENE_SCRIPT_H
#define SCENE_SCRIPT_H

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

// A property value crossing into and out of a script. Json is input only: nested data parsed into a JS object.
struct ScriptValue {
    enum class Kind { Number, Bool, Vec2, Vec3, String, Json };
    Kind kind = Kind::Number;
    double number = 0.0;  // Bool as 0 / 1
    double vec[3] = {0.0, 0.0, 0.0};
    std::string text;

    static ScriptValue makeNumber(double value);
    static ScriptValue makeBool(bool value);
    static ScriptValue makeVec2(double x, double y);
    static ScriptValue makeVec3(double x, double y, double z);
    static ScriptValue makeString(std::string value);
    static ScriptValue makeJson(std::string json);
};

using ScriptEvent = std::vector<std::pair<std::string, ScriptValue>>;

// One Wallpaper Engine SceneScript module running on the shared ScriptEngine.
class SceneScript {
   public:
    SceneScript();
    ~SceneScript();

    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    bool load(const std::string& source, const std::string& script_properties_json);

    // Replaces `value` with the result when types match; false when the hook is missing, threw or returned nothing usable.
    bool initValue(ScriptValue& value);
    bool updateValue(ScriptValue& value);

    // Calls an exported event hook; true when it exists and ran without throwing.
    bool callHook(const char* name, const ScriptEvent& event, bool with_event = true);

    void setLayerId(uint32_t layer_id);
    uint32_t layerId() const;
    int id() const;  // engine-wide script id used in logs
    // The scene this script belongs to (see ScriptEngine::registerScope), fixed when it loads.
    const void* scope() const;
    // The scene property this script drives, so thisObject.getAnimation() can find its timeline.
    void setProperty(const std::string& property);

    bool valid() const;  // loaded and exports update()
    bool hasFunction(const char* name) const;
    const std::string& lastError() const;
    const std::string& lastStack() const;
    int errorCount() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // SCENE_SCRIPT_H
