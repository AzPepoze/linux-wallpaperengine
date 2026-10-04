#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include <cstdint>
#include <string>

struct JSContext;
struct JSRuntime;

// Errors seen by one script, recorded from the shared QuickJS context.
struct ScriptErrors {
    std::string last;
    int count = 0;
};

// One QuickJS runtime and context shared by every SceneScript of the scene, so `shared`, `localStorage`, timers and
// the Wallpaper Engine globals exist once. The runtime is created on first retain() and freed with the last release().
class ScriptEngine {
   public:
    static ScriptEngine& instance();

    void retain();
    void release();
    JSContext* context() const {
        return context_;
    }

    // Frame inputs read by scripts through `engine.*`; also fires due setTimeout/setInterval callbacks.
    void beginFrame(double dt, double runtime_seconds, float canvas_w, float canvas_h, float screen_w, float screen_h);
    // Audio spectrum for engine.registerAudioBuffers(resolution) with resolution 16, 32 or 64.
    void setAudioBands(int resolution, const float* left, const float* right);
    // localStorage is persisted per wallpaper id under ~/.local/share/linux-wallpaperengine/localstorage.
    void setWallpaperId(const std::string& id);
    // The Wallpaper Engine `assets` folder. Its scripts/jsclasses/baseclasses.js (Vec2/3/4, Mat3/4,
    // createScriptProperties, shared...) is evaluated at startup and scripts/jsmodules/*.js back `import ... from
    // 'WEMath'` etc. Set before the first retain(); without it only a minimal built-in subset exists.
    void setAssetsDir(const std::string& assets_dir) {
        assets_dir_ = assets_dir;
    }
    const std::string& assetsDir() const {
        return assets_dir_;
    }
    // The scene scripts may query and modify (thisLayer / thisScene); not owned, clear it before the scene goes away.
    void setSceneBackend(class ScriptSceneBackend* backend) {
        scene_backend_ = backend;
    }
    class ScriptSceneBackend* sceneBackend() const {
        return scene_backend_;
    }
    void flushStorage();

    // Used by SceneScript around each script call.
    class CallScope {
       public:
        CallScope(ScriptEngine& engine, ScriptErrors* errors, int script_id, double budget_ms);
        ~CallScope();
        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;

       private:
        ScriptEngine& engine_;
        ScriptErrors* previous_errors_;
        int previous_id_;
    };
    ScriptErrors* currentErrors() const {
        return current_errors_;
    }
    int currentScriptId() const {
        return current_script_id_;
    }
    std::string wallpaperId() const {
        return wallpaper_id_;
    }
    int allocateScriptId() {
        return ++next_script_id_;
    }
    bool deadlineExceeded() const;

   private:
    ScriptEngine() = default;
    void create();
    void destroy();

    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    int refs_ = 0;
    int next_script_id_ = 0;
    int current_script_id_ = 0;
    ScriptErrors* current_errors_ = nullptr;
    int64_t deadline_ns_ = 0;
    std::string wallpaper_id_ = "default";
    std::string assets_dir_;
    class ScriptSceneBackend* scene_backend_ = nullptr;
    double storage_flush_timer_ = 0.0;
};

// Records the pending exception of `ctx` against the current script (log + SceneScript::lastError).
void scriptLogException(JSContext* ctx, const char* what);

#endif  // SCRIPT_ENGINE_H
