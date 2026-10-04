#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "scene_script.h"

struct JSContext;
struct JSRuntime;

struct ScriptErrors {
    std::string last;
    std::string stack;
    int count = 0;
};

// One QuickJS runtime shared by every SceneScript, so `shared`, `localStorage` and timers exist once. It is created
// by the first retain() and freed by the last release().
class ScriptEngine {
   public:
    static ScriptEngine& instance();

    void retain();
    void release();
    JSContext* context() const {
        return context_;
    }

    // The Wallpaper Engine assets folder supplies baseclasses.js and the WEMath/WEColor/WEVector modules; set it
    // before the first retain().
    void setAssetsDir(const std::string& assets_dir) {
        assets_dir_ = assets_dir;
    }
    const std::string& assetsDir() const {
        return assets_dir_;
    }
    void setWallpaperId(const std::string& id);
    void setSceneBackend(class ScriptSceneBackend* backend) {
        scene_backend_ = backend;
    }
    class ScriptSceneBackend* sceneBackend() const {
        return scene_backend_;
    }

    void beginFrame(double dt, double runtime_seconds, float canvas_w, float canvas_h, float screen_w, float screen_h);
    void setAudioBands(int resolution, const float* left, const float* right);
    void setInput(float world_x, float world_y, float screen_x, float screen_y, bool left_down);
    // engine.userProperties: one field per property (colors as Vec3).
    void setUserProperties(const ScriptEvent& properties);
    void flushStorage();

    void registerScript(SceneScript* script);
    void unregisterScript(SceneScript* script);
    size_t scriptCount() const {
        return scripts_.size();
    }
    // Both return how many scripts handled the event. A sticky broadcast is also delivered once to scripts that
    // load later.
    int broadcast(const char* hook, const ScriptEvent& event, bool sticky = false);
    int dispatchToLayer(uint32_t layer_id, const char* hook, const ScriptEvent& event);
    bool anyScriptExports(const std::vector<const char*>& hooks);
    std::vector<uint32_t> layersWithHooks(const std::vector<const char*>& hooks);
    void animationEnded(uint32_t handle);

    // Scopes one script call: where errors are recorded, which script is current, and its time budget.
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

    struct ScriptEntry {
        SceneScript* script = nullptr;
        bool sticky_delivered = false;
    };
    std::vector<ScriptEntry> scripts_;
    std::map<std::string, ScriptEvent> sticky_events_;
    double storage_flush_timer_ = 0.0;
};

// Records the pending exception of `ctx` against the current script.
void scriptLogException(JSContext* ctx, const char* what);

#endif  // SCRIPT_ENGINE_H
