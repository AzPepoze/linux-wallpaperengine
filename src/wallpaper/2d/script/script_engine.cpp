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

namespace {

int interruptHandler(JSRuntime*, void* opaque) {
    return static_cast<ScriptEngine*>(opaque)->deadlineExceeded() ? 1 : 0;
}

}  // namespace

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
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        LOG_TAG_W(TAG, "QuickJS runtime creation failed");
        return;
    }
    JS_SetMemoryLimit(runtime_, 64u * 1024u * 1024u);
    JS_SetInterruptHandler(runtime_, interruptHandler, this);
    installScriptModuleLoader(runtime_);
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        LOG_TAG_W(TAG, "QuickJS context creation failed");
        return;
    }
    installScriptHostFunctions(context_);

    std::string base_classes;
    if (!assets_dir_.empty()) base_classes = readScriptFile(assets_dir_ + "/scripts/jsclasses/baseclasses.js");
    if (base_classes.empty())
        LOG_TAG_W(
            TAG, "baseclasses.js not found under the assets folder; Vec2/Vec3/Mat4 and WEMath modules are unavailable");

    struct Chunk {
        const char* name;
        const std::string* text;
    };
    const std::string prelude = scriptPreludeSource();
    const Chunk chunks[] = {{"baseclasses.js", &base_classes}, {"<script-prelude>", &prelude}};
    for (const Chunk& chunk : chunks) {
        if (chunk.text->empty()) continue;
        CallScope scope(*this, nullptr, 0, 500.0);
        JSValue result = JS_Eval(context_, chunk.text->c_str(), chunk.text->size(), chunk.name, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) scriptLogException(context_, chunk.name);
        JS_FreeValue(context_, result);
    }
}

void ScriptEngine::destroy() {
    if (context_) {
        flushStorage();
        JS_FreeContext(context_);
    }
    if (runtime_) JS_FreeRuntime(runtime_);
    context_ = nullptr;
    runtime_ = nullptr;
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
    if (!context_) return;
    CallScope scope(*this, nullptr, 0, 20.0);
    JSValue global = JS_GetGlobalObject(context_);
    JSValue setter = JS_GetPropertyStr(context_, global, "__lweSetUserProperties");
    JSValue object = toJsObject(context_, properties);
    JSValue result = JS_Call(context_, setter, JS_UNDEFINED, 1, &object);
    if (JS_IsException(result)) scriptLogException(context_, "userProperties");
    JS_FreeValue(context_, result);
    JS_FreeValue(context_, object);
    JS_FreeValue(context_, setter);
    JS_FreeValue(context_, global);
}

void ScriptEngine::animationEnded(uint32_t handle) {
    if (!context_) return;
    CallScope scope(*this, nullptr, 0, 20.0);
    JSValue global = JS_GetGlobalObject(context_);
    JSValue notify = JS_GetPropertyStr(context_, global, "__lweAnimationEnded");
    JSValue argument = JS_NewUint32(context_, handle);
    JSValue result = JS_Call(context_, notify, JS_UNDEFINED, 1, &argument);
    if (JS_IsException(result)) scriptLogException(context_, "animationEnded");
    JS_FreeValue(context_, result);
    JS_FreeValue(context_, argument);
    JS_FreeValue(context_, notify);
    JS_FreeValue(context_, global);
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
    if (!context_) return;
    CallScope scope(*this, nullptr, 0, 10.0);
    JSValue global = JS_GetGlobalObject(context_);
    JSValue set_input = JS_GetPropertyStr(context_, global, "__lweSetInput");
    JSValue args[5] = {JS_NewFloat64(context_, world_x), JS_NewFloat64(context_, world_y),
                       JS_NewFloat64(context_, screen_x), JS_NewFloat64(context_, screen_y),
                       JS_NewBool(context_, left_down)};
    JSValue result = JS_Call(context_, set_input, JS_UNDEFINED, 5, args);
    if (JS_IsException(result)) scriptLogException(context_, "input");
    JS_FreeValue(context_, result);
    for (JSValue& arg : args) JS_FreeValue(context_, arg);
    JS_FreeValue(context_, set_input);
    JS_FreeValue(context_, global);
}

bool ScriptEngine::deadlineExceeded() const {
    return deadline_ns_ != 0 && scriptNowNs() > deadline_ns_;
}

ScriptEngine::CallScope::CallScope(ScriptEngine& engine, ScriptErrors* errors, int script_id, double budget_ms)
    : engine_(engine), previous_errors_(engine.current_errors_), previous_id_(engine.current_script_id_) {
    engine.current_errors_ = errors;
    engine.current_script_id_ = script_id;
    engine.deadline_ns_ = scriptNowNs() + (int64_t)(budget_ms * 1e6);
}

ScriptEngine::CallScope::~CallScope() {
    engine_.current_errors_ = previous_errors_;
    engine_.current_script_id_ = previous_id_;
    engine_.deadline_ns_ = 0;
}

void ScriptEngine::beginFrame(double dt, double runtime_seconds, float canvas_w, float canvas_h, float screen_w,
                              float screen_h) {
    if (!context_) return;
    CallScope scope(*this, nullptr, 0, 20.0);
    JSValue global = JS_GetGlobalObject(context_);
    JSValue tick = JS_GetPropertyStr(context_, global, "__lweTick");
    JSValue args[6] = {JS_NewFloat64(context_, dt),       JS_NewFloat64(context_, runtime_seconds),
                       JS_NewFloat64(context_, canvas_w), JS_NewFloat64(context_, canvas_h),
                       JS_NewFloat64(context_, screen_w), JS_NewFloat64(context_, screen_h)};
    JSValue result = JS_Call(context_, tick, JS_UNDEFINED, 6, args);
    if (JS_IsException(result)) scriptLogException(context_, "tick");
    JS_FreeValue(context_, result);
    for (JSValue& arg : args) JS_FreeValue(context_, arg);
    JS_FreeValue(context_, tick);
    JS_FreeValue(context_, global);

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
    if (!context_ || (resolution != 16 && resolution != 32 && resolution != 64)) return;
    JSValue global = JS_GetGlobalObject(context_);
    JSValue all = JS_GetPropertyStr(context_, global, "__lweAudio");
    JSValue bands = JS_GetPropertyUint32(context_, all, (uint32_t)resolution);
    if (JS_IsObject(bands)) {
        JSValue average = JS_GetPropertyStr(context_, bands, "average");
        JSValue left_array = JS_GetPropertyStr(context_, bands, "left");
        JSValue right_array = JS_GetPropertyStr(context_, bands, "right");
        for (int i = 0; i < resolution; ++i) {
            JS_SetPropertyUint32(context_, left_array, (uint32_t)i, JS_NewFloat64(context_, left[i]));
            JS_SetPropertyUint32(context_, right_array, (uint32_t)i, JS_NewFloat64(context_, right[i]));
            JS_SetPropertyUint32(context_, average, (uint32_t)i, JS_NewFloat64(context_, (left[i] + right[i]) * 0.5));
        }
        JS_FreeValue(context_, average);
        JS_FreeValue(context_, left_array);
        JS_FreeValue(context_, right_array);
    }
    JS_FreeValue(context_, bands);
    JS_FreeValue(context_, all);
    JS_FreeValue(context_, global);
}

void ScriptEngine::setWallpaperId(const std::string& id) {
    wallpaper_id_ = id.empty() ? "default" : id;
}

void ScriptEngine::flushStorage() {
    if (!context_) return;
    CallScope scope(*this, nullptr, 0, 50.0);
    JSValue global = JS_GetGlobalObject(context_);
    JSValue flush = JS_GetPropertyStr(context_, global, "__lweStorageFlush");
    if (JS_IsFunction(context_, flush)) {
        JSValue result = JS_Call(context_, flush, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(result)) scriptLogException(context_, "storage");
        JS_FreeValue(context_, result);
    }
    JS_FreeValue(context_, flush);
    JS_FreeValue(context_, global);
}
