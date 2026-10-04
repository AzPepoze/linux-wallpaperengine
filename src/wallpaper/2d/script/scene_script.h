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

    // Property scripts (e.g. an image's alpha): `init()` yields the starting value, `update(value)` the next one.
    // False when the script does not define the hook or it returned a non-number.
    bool callInit(double& out);
    bool updateNumber(double value, double& out);

    // Per-frame inputs the script reads: `engine.frametime` and the 16-band audio average (0 when silent).
    void setFrameTime(double seconds);
    void setAudioAverage(const float* bands, int count);

    // Calls a single-string-argument hook such as `mediaPropertiesChanged`.
    void callWithString(const std::string& function, const std::string& argument);

    // Calls `mediaPropertiesChanged({ title })` for media-title scripts.
    void mediaPropertiesChanged(const std::string& title);

    bool valid() const;

    // Diagnostics: the most recent script exception ("hook: message"), how many have been seen, and whether the
    // script defines a global hook of that name.
    const std::string& lastError() const;
    int errorCount() const;
    bool hasFunction(const char* name) const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // SCENE_SCRIPT_H
