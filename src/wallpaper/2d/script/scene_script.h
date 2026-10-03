#ifndef SCENE_SCRIPT_H
#define SCENE_SCRIPT_H

#include <memory>
#include <string>

// Minimal Wallpaper Engine SceneScript host backed by QuickJS.
//
// Wallpaper Engine runs object scripts in an embedded V8. The engine only needs
// the text-object subset for now: a module-level `update(value)` that returns
// the next string, the `createScriptProperties` builder and the scene's
// `scriptproperties` overrides (used by clocks and media titles).
class SceneScript {
   public:
    SceneScript();
    ~SceneScript();

    SceneScript(const SceneScript&) = delete;
    SceneScript& operator=(const SceneScript&) = delete;

    // Evaluates an ES module text script (stripping `export` keywords) and
    // applies `script_properties_json` overrides. Returns false when the source
    // is empty or fails to compile.
    bool load(const std::string& source, const std::string& script_properties_json);

    // Calls `update(value)` and stores the returned string in `out`. Returns
    // false when the script has no callable `update`.
    bool update(const std::string& value, std::string& out);

    // Calls a single-string-argument hook such as `mediaPropertiesChanged`.
    // Missing functions are ignored.
    void callWithString(const std::string& function, const std::string& argument);

    // Calls `mediaPropertiesChanged({ title })` so media-title scripts update
    // their cached track name.
    void mediaPropertiesChanged(const std::string& title);

    bool valid() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // SCENE_SCRIPT_H
