#include "scene_script.h"

#include <quickjs.h>

#include <cstring>

#include "script_engine.h"
#include "script_value_js.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {

constexpr const char* kHooks[] = {"init",
                                  "update",
                                  "destroy",
                                  "resizeScreen",
                                  "applyUserProperties",
                                  "applyGeneralSettings",
                                  "cursorEnter",
                                  "cursorLeave",
                                  "cursorMove",
                                  "cursorDown",
                                  "cursorUp",
                                  "cursorClick",
                                  "mediaStatusChanged",
                                  "mediaPlaybackChanged",
                                  "mediaPropertiesChanged",
                                  "mediaThumbnailChanged",
                                  "mediaTimelineChanged"};

constexpr int kMaxConsecutiveErrors = 3;
constexpr double kLoadBudgetMs = 500.0;
constexpr double kCallBudgetMs = 20.0;

// This QuickJS has no public module-namespace accessor, so the module's exports are copied into a table by an
// epilogue appended to its source.
std::string buildModuleSource(const std::string& source, int id) {
    std::string text = source;
    text += "\n;globalThis.__lweExports[" + std::to_string(id) + "] = {";
    for (const char* hook : kHooks)
        text += std::string(hook) + ": typeof " + hook + " === 'function' ? " + hook + " : undefined,";
    text += "scriptProperties: typeof scriptProperties !== 'undefined' ? scriptProperties : undefined};\n";
    return text;
}

}  // namespace

struct SceneScript::Impl {
    bool retained = false;
    int id = 0;
    ScriptErrors errors;
    JSValue exports = JS_UNDEFINED;
    JSValue init_fn = JS_UNDEFINED;
    JSValue update_fn = JS_UNDEFINED;
    JSValue context_object = JS_UNDEFINED;
    int consecutive_errors = 0;
    bool disabled = false;
    uint32_t layer_id = 0;
    std::string property;
    const void* scope = nullptr;

    JSContext* ctx() const {
        return ScriptEngine::instance().context();
    }

    // The result is owned by the caller.
    bool call(JSValueConst fn, const char* what, int argc, JSValue* argv, JSValue& result, double budget_ms) {
        result = JS_UNDEFINED;
        JSContext* c = ctx();
        if (!c || disabled || !JS_IsFunction(c, fn)) return false;

        JSValue global = JS_GetGlobalObject(c);
        JS_SetPropertyStr(c, global, "__lweCurrent", JS_DupValue(c, context_object));
        JS_FreeValue(c, global);

        ScriptEngine::CallScope call_scope(ScriptEngine::instance(), &errors, id, budget_ms, scope);
        result = JS_Call(c, fn, JS_UNDEFINED, argc, argv);
        if (JS_IsException(result)) {
            scriptLogException(c, what);
            result = JS_UNDEFINED;
            if (++consecutive_errors >= kMaxConsecutiveErrors) {
                disabled = true;
                LOG_TAG_W(TAG, "script %d disabled after %d consecutive errors", id, consecutive_errors);
            }
            return false;
        }
        consecutive_errors = 0;
        return true;
    }

    JSValue hook(const char* name) const {
        JSContext* c = ctx();
        if (!c || !JS_IsObject(exports)) return JS_UNDEFINED;
        return JS_GetPropertyStr(c, exports, name);
    }
};

ScriptValue ScriptValue::makeNumber(double value) {
    ScriptValue result;
    result.number = value;
    return result;
}

ScriptValue ScriptValue::makeBool(bool value) {
    ScriptValue result;
    result.kind = Kind::Bool;
    result.number = value ? 1.0 : 0.0;
    return result;
}

ScriptValue ScriptValue::makeVec2(double x, double y) {
    ScriptValue result;
    result.kind = Kind::Vec2;
    result.vec[0] = x;
    result.vec[1] = y;
    return result;
}

ScriptValue ScriptValue::makeVec3(double x, double y, double z) {
    ScriptValue result;
    result.kind = Kind::Vec3;
    result.vec[0] = x;
    result.vec[1] = y;
    result.vec[2] = z;
    return result;
}

ScriptValue ScriptValue::makeString(std::string value) {
    ScriptValue result;
    result.kind = Kind::String;
    result.text = std::move(value);
    return result;
}

ScriptValue ScriptValue::makeJson(std::string json) {
    ScriptValue result;
    result.kind = Kind::Json;
    result.text = std::move(json);
    return result;
}

SceneScript::SceneScript() : impl_(std::make_unique<Impl>()) {}

SceneScript::~SceneScript() {
    if (!impl_ || !impl_->retained) return;
    ScriptEngine& engine = ScriptEngine::instance();
    engine.unregisterScript(this);
    if (JSContext* c = engine.context()) {
        JS_FreeValue(c, impl_->exports);
        JS_FreeValue(c, impl_->init_fn);
        JS_FreeValue(c, impl_->update_fn);
        JS_FreeValue(c, impl_->context_object);
        JSValue global = JS_GetGlobalObject(c);
        JSValue table = JS_GetPropertyStr(c, global, "__lweExports");
        if (JS_IsObject(table)) {
            JSAtom atom = JS_NewAtom(c, std::to_string(impl_->id).c_str());
            JS_DeleteProperty(c, table, atom, 0);
            JS_FreeAtom(c, atom);
        }
        JS_FreeValue(c, table);
        JS_FreeValue(c, global);
    }
    engine.release();
}

const std::string& SceneScript::lastError() const {
    static const std::string none;
    return impl_ ? impl_->errors.last : none;
}

const std::string& SceneScript::lastStack() const {
    static const std::string none;
    return impl_ ? impl_->errors.stack : none;
}

int SceneScript::errorCount() const {
    return impl_ ? impl_->errors.count : 0;
}

bool SceneScript::hasFunction(const char* name) const {
    if (!impl_ || !impl_->retained) return false;
    JSValue fn = impl_->hook(name);
    const bool found = JS_IsFunction(impl_->ctx(), fn);
    JS_FreeValue(impl_->ctx(), fn);
    return found;
}

bool SceneScript::valid() const {
    return impl_ && impl_->retained && JS_IsFunction(impl_->ctx(), impl_->update_fn);
}

bool SceneScript::load(const std::string& source, const std::string& script_properties_json) {
    if (source.empty() || !impl_ || impl_->retained) return false;

    ScriptEngine& engine = ScriptEngine::instance();
    engine.retain();
    impl_->retained = true;
    JSContext* ctx = engine.context();
    if (!ctx) return false;
    impl_->id = engine.allocateScriptId();
    impl_->scope = engine.creationScope();
    const std::string name = "script://" + std::to_string(impl_->id);
    const std::string module = buildModuleSource(source, impl_->id);

    // Top-level code already runs as this script (thisLayer, shared, localStorage).
    impl_->context_object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, impl_->context_object, "id", JS_NewInt32(ctx, impl_->id));
    JS_SetPropertyStr(ctx, impl_->context_object, "layerId", JS_NewUint32(ctx, impl_->layer_id));
    JS_SetPropertyStr(ctx, impl_->context_object, "scopeKey", JS_NewInt32(ctx, engine.scopeKey(impl_->scope)));
    JS_SetPropertyStr(ctx, impl_->context_object, "property",
                      JS_NewStringLen(ctx, impl_->property.c_str(), impl_->property.size()));
    {
        JSValue current_global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, current_global, "__lweCurrent", JS_DupValue(ctx, impl_->context_object));
        JS_FreeValue(ctx, current_global);
    }

    ScriptEngine::CallScope scope(engine, &impl_->errors, impl_->id, kLoadBudgetMs, impl_->scope);
    JSValue promise = JS_Eval(ctx, module.c_str(), module.size(), name.c_str(), JS_EVAL_TYPE_MODULE);
    if (JS_IsException(promise)) {
        scriptLogException(ctx, "compile");
        return false;
    }

    // Module evaluation is a promise; a rejection (a throw at the top level) is recorded by __lweWatch.
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue watch = JS_GetPropertyStr(ctx, global, "__lweWatch");
    JSValue watch_args[2] = {promise, JS_NewInt32(ctx, impl_->id)};
    JSValue watched = JS_Call(ctx, watch, JS_UNDEFINED, 2, watch_args);
    JS_FreeValue(ctx, watched);
    JS_FreeValue(ctx, watch);
    JS_FreeValue(ctx, promise);

    JSContext* job_context = nullptr;
    while (JS_ExecutePendingJob(JS_GetRuntime(ctx), &job_context) > 0) {
    }

    bool loaded = false;
    JSValue errors = JS_GetPropertyStr(ctx, global, "__lweErrors");
    JSValue error = JS_GetPropertyStr(ctx, errors, std::to_string(impl_->id).c_str());
    if (JS_IsString(error)) {
        const char* message = JS_ToCString(ctx, error);
        impl_->errors.last = std::string("load: ") + (message ? message : "(unknown)");
        ++impl_->errors.count;
        LOG_TAG_W(TAG, "script %d load error: %s", impl_->id, message ? message : "(unknown)");
        if (message) JS_FreeCString(ctx, message);
    } else {
        JSValue table = JS_GetPropertyStr(ctx, global, "__lweExports");
        impl_->exports = JS_GetPropertyStr(ctx, table, std::to_string(impl_->id).c_str());
        JS_FreeValue(ctx, table);
        loaded = JS_IsObject(impl_->exports);
    }
    JS_FreeValue(ctx, error);
    JS_FreeValue(ctx, errors);

    if (loaded) {
        if (!script_properties_json.empty()) {
            JSValue apply = JS_GetPropertyStr(ctx, global, "__lweApplyOverrides");
            JSValue apply_args[2] = {
                impl_->exports, JS_NewStringLen(ctx, script_properties_json.c_str(), script_properties_json.size())};
            JSValue applied = JS_Call(ctx, apply, JS_UNDEFINED, 2, apply_args);
            if (JS_IsException(applied)) scriptLogException(ctx, "scriptproperties");
            JS_FreeValue(ctx, applied);
            JS_FreeValue(ctx, apply_args[1]);
            JS_FreeValue(ctx, apply);
        }
        impl_->init_fn = impl_->hook("init");
        impl_->update_fn = impl_->hook("update");
        engine.registerScript(this);
    }
    JS_FreeValue(ctx, global);
    return loaded;
}

bool SceneScript::initValue(ScriptValue& value) {
    if (!impl_ || !impl_->retained || !JS_IsFunction(impl_->ctx(), impl_->init_fn)) return false;
    JSContext* ctx = impl_->ctx();
    JSValue argument = toJsValue(ctx, value);
    JSValue result;
    bool ok = impl_->call(impl_->init_fn, "init", 1, &argument, result, kLoadBudgetMs);
    JS_FreeValue(ctx, argument);
    ok = ok && fromJsValue(ctx, result, value);
    JS_FreeValue(ctx, result);
    return ok;
}

bool SceneScript::updateValue(ScriptValue& value) {
    if (!valid()) return false;
    JSContext* ctx = impl_->ctx();
    JSValue argument = toJsValue(ctx, value);
    JSValue result;
    bool ok = impl_->call(impl_->update_fn, "update", 1, &argument, result, kCallBudgetMs);
    JS_FreeValue(ctx, argument);
    ok = ok && fromJsValue(ctx, result, value);
    JS_FreeValue(ctx, result);
    return ok;
}

bool SceneScript::callHook(const char* name, const ScriptEvent& event, bool with_event) {
    if (!impl_ || !impl_->retained) return false;
    JSContext* ctx = impl_->ctx();
    JSValue fn = impl_->hook(name);
    bool ok = false;
    if (JS_IsFunction(ctx, fn)) {
        JSValue argument = JS_UNDEFINED;
        if (with_event || !event.empty()) argument = toJsObject(ctx, event);
        JSValue result;
        ok = impl_->call(fn, name, JS_IsUndefined(argument) ? 0 : 1, &argument, result, kCallBudgetMs);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, argument);
    }
    JS_FreeValue(ctx, fn);
    return ok;
}

void SceneScript::setLayerId(uint32_t layer_id) {
    if (!impl_) return;
    impl_->layer_id = layer_id;
    if (impl_->retained && JS_IsObject(impl_->context_object))
        JS_SetPropertyStr(impl_->ctx(), impl_->context_object, "layerId", JS_NewUint32(impl_->ctx(), layer_id));
}

uint32_t SceneScript::layerId() const {
    return impl_ ? impl_->layer_id : 0;
}

const void* SceneScript::scope() const {
    return impl_ ? impl_->scope : nullptr;
}

void SceneScript::setProperty(const std::string& property) {
    if (!impl_) return;
    impl_->property = property;
    if (impl_->retained && JS_IsObject(impl_->context_object))
        JS_SetPropertyStr(impl_->ctx(), impl_->context_object, "property",
                          JS_NewStringLen(impl_->ctx(), property.c_str(), property.size()));
}
