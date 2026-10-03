#ifndef SCENE_SCRIPT_H
#define SCENE_SCRIPT_H

#include <memory>
#include <string>

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

    // Calls a single-string-argument hook such as `mediaPropertiesChanged`.
    void callWithString(const std::string& function, const std::string& argument);

    // Calls `mediaPropertiesChanged({ title })` for media-title scripts.
    void mediaPropertiesChanged(const std::string& title);

    bool valid() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // SCENE_SCRIPT_H
