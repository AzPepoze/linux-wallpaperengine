#include "script_engine.h"

#include <quickjs.h>

#include <chrono>
#include <cstdint>

#include "script_engine_internal.h"
#include "script_value_js.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

int64_t scriptNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void scriptLogException(JSContext* ctx, const char* what) {
    JSValue exception = JS_GetException(ctx);
    const char* message = JS_ToCString(ctx, exception);
    ScriptEngine& engine = ScriptEngine::instance();
    LOG_TAG_W(TAG, "script %d %s error: %s", engine.currentScriptId(), what, message ? message : "(unknown)");
    if (ScriptErrors* errors = engine.currentErrors()) {
        errors->last = std::string(what) + ": " + (message ? message : "(unknown)");
        errors->stack.clear();
        JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
        if (const char* frames = JS_IsString(stack) ? JS_ToCString(ctx, stack) : nullptr) {
            errors->stack = frames;
            JS_FreeCString(ctx, frames);
        }
        JS_FreeValue(ctx, stack);
        ++errors->count;
    }
    if (message) JS_FreeCString(ctx, message);
    JS_FreeValue(ctx, exception);
}

ScriptEngine& ScriptEngine::instance() {
    static ScriptEngine engine;
    return engine;
}

void ScriptEngine::retain() {
    if (refs_++ == 0) create();
}

void ScriptEngine::release() {
    if (refs_ > 0 && --refs_ == 0) destroy();
}

void ScriptEngine::create() {
    runtime_.create(assets_dir_);
}

void ScriptEngine::destroy() {
    if (runtime_.context()) flushStorage();
    runtime_.destroy();
}

void ScriptEngine::registerScript(SceneScript* script) {
    for (const ScriptEntry& entry : scripts_)
        if (entry.script == script) return;
    scripts_.push_back({script, false});
}

void ScriptEngine::unregisterScript(SceneScript* script) {
    for (size_t i = 0; i < scripts_.size(); ++i) {
        if (scripts_[i].script == script) {
            scripts_.erase(scripts_.begin() + (long)i);
            return;
        }
    }
}

int ScriptEngine::broadcast(const char* hook, const ScriptEvent& event, bool sticky) {
    if (sticky) sticky_events_[hook] = event;
    int delivered = 0;
    const std::vector<ScriptEntry> entries = scripts_;  // a hook may load or free scripts
    for (const ScriptEntry& entry : entries) {
        if (!entry.script->callHook(hook, event)) continue;
        ++delivered;
    }
    return delivered;
}

int ScriptEngine::dispatchToLayer(uint32_t layer_id, const char* hook, const ScriptEvent& event) {
    int delivered = 0;
    const std::vector<ScriptEntry> entries = scripts_;
    for (const ScriptEntry& entry : entries)
        if (entry.script->layerId() == layer_id && entry.script->callHook(hook, event)) ++delivered;
    return delivered;
}

void ScriptEngine::setUserProperties(const ScriptEvent& properties) {
    if (!runtime_.context()) return;
    CallScope scope(*this, nullptr, 0, 20.0);
    JSValue global = JS_GetGlobalObject(runtime_.context());
    JSValue setter = JS_GetPropertyStr(runtime_.context(), global, "__lweSetUserProperties");
    JSValue object = toJsObject(runtime_.context(), properties);
    JSValue result = JS_Call(runtime_.context(), setter, JS_UNDEFINED, 1, &object);
    if (JS_IsException(result)) scriptLogException(runtime_.context(), "userProperties");
    JS_FreeValue(runtime_.context(), result);
    JS_FreeValue(runtime_.context(), object);
    JS_FreeValue(runtime_.context(), setter);
    JS_FreeValue(runtime_.context(), global);
}

void ScriptEngine::animationEnded(uint32_t handle) {
    if (!runtime_.context()) return;
    CallScope scope(*this, nullptr, 0, 20.0);
    JSValue global = JS_GetGlobalObject(runtime_.context());
    JSValue notify = JS_GetPropertyStr(runtime_.context(), global, "__lweAnimationEnded");
    JSValue argument = JS_NewUint32(runtime_.context(), handle);
    JSValue result = JS_Call(runtime_.context(), notify, JS_UNDEFINED, 1, &argument);
    if (JS_IsException(result)) scriptLogException(runtime_.context(), "animationEnded");
    JS_FreeValue(runtime_.context(), result);
    JS_FreeValue(runtime_.context(), argument);
    JS_FreeValue(runtime_.context(), notify);
    JS_FreeValue(runtime_.context(), global);
}

bool ScriptEngine::anyScriptExports(const std::vector<const char*>& hooks) {
    for (const ScriptEntry& entry : scripts_)
        for (const char* hook : hooks)
            if (entry.script->hasFunction(hook)) return true;
    return false;
}

std::vector<uint32_t> ScriptEngine::layersWithHooks(const std::vector<const char*>& hooks) {
    std::vector<uint32_t> ids;
    for (const ScriptEntry& entry : scripts_) {
        if (entry.script->layerId() == 0) continue;
        for (const char* hook : hooks) {
            if (!entry.script->hasFunction(hook)) continue;
            ids.push_back(entry.script->layerId());
            break;
        }
    }
    return ids;
}

void ScriptEngine::setInput(float world_x, float world_y, float screen_x, float screen_y, bool left_down) {
    if (!runtime_.context()) return;
    CallScope scope(*this, nullptr, 0, 10.0);
    JSValue global = JS_GetGlobalObject(runtime_.context());
    JSValue set_input = JS_GetPropertyStr(runtime_.context(), global, "__lweSetInput");
    JSValue args[5] = {JS_NewFloat64(runtime_.context(), world_x), JS_NewFloat64(runtime_.context(), world_y),
                       JS_NewFloat64(runtime_.context(), screen_x), JS_NewFloat64(runtime_.context(), screen_y),
                       JS_NewBool(runtime_.context(), left_down)};
    JSValue result = JS_Call(runtime_.context(), set_input, JS_UNDEFINED, 5, args);
    if (JS_IsException(result)) scriptLogException(runtime_.context(), "input");
    JS_FreeValue(runtime_.context(), result);
    for (JSValue& arg : args) JS_FreeValue(runtime_.context(), arg);
    JS_FreeValue(runtime_.context(), set_input);
    JS_FreeValue(runtime_.context(), global);
}

bool ScriptEngine::deadlineExceeded() const {
    return runtime_.deadlineExceeded();
}

ScriptEngine::CallScope::CallScope(ScriptEngine& engine, ScriptErrors* errors, int script_id, double budget_ms)
    : engine_(engine), previous_errors_(engine.current_errors_), previous_id_(engine.current_script_id_) {
    engine.current_errors_ = errors;
    engine.current_script_id_ = script_id;
    engine.runtime_.setDeadlineNs(scriptNowNs() + (int64_t)(budget_ms * 1e6));
}

ScriptEngine::CallScope::~CallScope() {
    engine_.current_errors_ = previous_errors_;
    engine_.current_script_id_ = previous_id_;
    engine_.runtime_.setDeadlineNs(0);
}

void ScriptEngine::beginFrame(double dt, double runtime_seconds, float canvas_w, float canvas_h, float screen_w,
                              float screen_h) {
    if (!runtime_.context()) return;
    CallScope scope(*this, nullptr, 0, 20.0);
    JSValue global = JS_GetGlobalObject(runtime_.context());
    JSValue tick = JS_GetPropertyStr(runtime_.context(), global, "__lweTick");
    JSValue args[6] = {JS_NewFloat64(runtime_.context(), dt),       JS_NewFloat64(runtime_.context(), runtime_seconds),
                       JS_NewFloat64(runtime_.context(), canvas_w), JS_NewFloat64(runtime_.context(), canvas_h),
                       JS_NewFloat64(runtime_.context(), screen_w), JS_NewFloat64(runtime_.context(), screen_h)};
    JSValue result = JS_Call(runtime_.context(), tick, JS_UNDEFINED, 6, args);
    if (JS_IsException(result)) scriptLogException(runtime_.context(), "tick");
    JS_FreeValue(runtime_.context(), result);
    for (JSValue& arg : args) JS_FreeValue(runtime_.context(), arg);
    JS_FreeValue(runtime_.context(), tick);
    JS_FreeValue(runtime_.context(), global);

    for (size_t i = 0; i < scripts_.size(); ++i) {
        if (scripts_[i].sticky_delivered) continue;
        scripts_[i].sticky_delivered = true;
        SceneScript* script = scripts_[i].script;
        const std::map<std::string, ScriptEvent> events = sticky_events_;
        for (const auto& [hook, event] : events) script->callHook(hook.c_str(), event);
    }

    storage_flush_timer_ += dt;
    if (storage_flush_timer_ >= 2.0) {
        storage_flush_timer_ = 0.0;
        flushStorage();
    }
}

void ScriptEngine::setAudioBands(int resolution, const float* left, const float* right) {
    if (!runtime_.context() || (resolution != 16 && resolution != 32 && resolution != 64)) return;
    JSValue global = JS_GetGlobalObject(runtime_.context());
    JSValue all = JS_GetPropertyStr(runtime_.context(), global, "__lweAudio");
    JSValue bands = JS_GetPropertyUint32(runtime_.context(), all, (uint32_t)resolution);
    if (JS_IsObject(bands)) {
        JSValue average = JS_GetPropertyStr(runtime_.context(), bands, "average");
        JSValue left_array = JS_GetPropertyStr(runtime_.context(), bands, "left");
        JSValue right_array = JS_GetPropertyStr(runtime_.context(), bands, "right");
        for (int i = 0; i < resolution; ++i) {
            JS_SetPropertyUint32(runtime_.context(), left_array, (uint32_t)i,
                                 JS_NewFloat64(runtime_.context(), left[i]));
            JS_SetPropertyUint32(runtime_.context(), right_array, (uint32_t)i,
                                 JS_NewFloat64(runtime_.context(), right[i]));
            JS_SetPropertyUint32(runtime_.context(), average, (uint32_t)i,
                                 JS_NewFloat64(runtime_.context(), (left[i] + right[i]) * 0.5));
        }
        JS_FreeValue(runtime_.context(), average);
        JS_FreeValue(runtime_.context(), left_array);
        JS_FreeValue(runtime_.context(), right_array);
    }
    JS_FreeValue(runtime_.context(), bands);
    JS_FreeValue(runtime_.context(), all);
    JS_FreeValue(runtime_.context(), global);
}

void ScriptEngine::setWallpaperId(const std::string& id) {
    wallpaper_id_ = id.empty() ? "default" : id;
}

void ScriptEngine::flushStorage() {
    if (!runtime_.context()) return;
    CallScope scope(*this, nullptr, 0, 50.0);
    JSValue global = JS_GetGlobalObject(runtime_.context());
    JSValue flush = JS_GetPropertyStr(runtime_.context(), global, "__lweStorageFlush");
    if (JS_IsFunction(runtime_.context(), flush)) {
        JSValue result = JS_Call(runtime_.context(), flush, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(result)) scriptLogException(runtime_.context(), "storage");
        JS_FreeValue(runtime_.context(), result);
    }
    JS_FreeValue(runtime_.context(), flush);
    JS_FreeValue(runtime_.context(), global);
}
