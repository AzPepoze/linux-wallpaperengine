#include "scene_script.h"

#include <quickjs.h>

#include <cstring>

#include "script_engine.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {

// Event handlers a script may export; collected into __lweExports[id] by an epilogue appended to the module, because
// this QuickJS version has no public module-namespace accessor.
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

// Builds a Vec2 / Vec3 through the global constructor from baseclasses.js, or a plain {x, y, z} object without it.
JSValue makeVector(JSContext* ctx, const char* constructor, const double* v, int count) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, constructor);
    JS_FreeValue(ctx, global);
    JSValue args[3] = {JS_NewFloat64(ctx, v[0]), JS_NewFloat64(ctx, v[1]), JS_NewFloat64(ctx, count > 2 ? v[2] : 0.0)};
    JSValue result;
    if (JS_IsFunction(ctx, ctor)) {
        result = JS_CallConstructor(ctx, ctor, count, args);
    } else {
        result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "x", JS_DupValue(ctx, args[0]));
        JS_SetPropertyStr(ctx, result, "y", JS_DupValue(ctx, args[1]));
        if (count > 2) JS_SetPropertyStr(ctx, result, "z", JS_DupValue(ctx, args[2]));
    }
    for (JSValue& arg : args) JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, ctor);
    return result;
}

JSValue toJsValue(JSContext* ctx, const ScriptValue& value) {
    switch (value.kind) {
        case ScriptValue::Kind::Number:
            return JS_NewFloat64(ctx, value.number);
        case ScriptValue::Kind::Bool:
            return JS_NewBool(ctx, value.number != 0.0);
        case ScriptValue::Kind::Vec2:
            return makeVector(ctx, "Vec2", value.vec, 2);
        case ScriptValue::Kind::Vec3:
            return makeVector(ctx, "Vec3", value.vec, 3);
        case ScriptValue::Kind::String:
            return JS_NewStringLen(ctx, value.text.c_str(), value.text.size());
        case ScriptValue::Kind::Json: {
            JSValue parsed = JS_ParseJSON(ctx, value.text.c_str(), value.text.size(), "<event>");
            if (JS_IsException(parsed)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                return JS_UNDEFINED;
            }
            return parsed;
        }
    }
    return JS_UNDEFINED;
}

bool readNumber(JSContext* ctx, JSValueConst value, double& out) {
    double number = 0.0;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) != 0 || number != number) return false;
    out = number;
    return true;
}

bool readComponent(JSContext* ctx, JSValueConst object, const char* key, double& out) {
    JSValue component = JS_GetPropertyStr(ctx, object, key);
    const bool ok = readNumber(ctx, component, out);
    JS_FreeValue(ctx, component);
    return ok;
}

// Replaces `value` with the script's result when it has a compatible type.
bool fromJsValue(JSContext* ctx, JSValueConst result, ScriptValue& value) {
    switch (value.kind) {
        case ScriptValue::Kind::Number:
            return readNumber(ctx, result, value.number);
        case ScriptValue::Kind::Bool: {
            if (JS_IsBool(result)) {
                value.number = JS_ToBool(ctx, result) ? 1.0 : 0.0;
                return true;
            }
            double number = 0.0;
            if (!readNumber(ctx, result, number)) return false;
            value.number = number != 0.0 ? 1.0 : 0.0;
            return true;
        }
        case ScriptValue::Kind::Json:
            return false;
        case ScriptValue::Kind::String: {
            if (JS_IsUndefined(result) || JS_IsNull(result)) return false;
            const char* text = JS_ToCString(ctx, result);
            if (!text) return false;
            value.text = text;
            JS_FreeCString(ctx, text);
            return true;
        }
        case ScriptValue::Kind::Vec2:
        case ScriptValue::Kind::Vec3: {
            const int count = value.kind == ScriptValue::Kind::Vec3 ? 3 : 2;
            double parsed[3] = {value.vec[0], value.vec[1], value.vec[2]};
            if (JS_IsNumber(result)) {  // a number broadcasts to every component
                double scalar = 0.0;
                if (!readNumber(ctx, result, scalar)) return false;
                for (int i = 0; i < count; ++i) parsed[i] = scalar;
            } else if (JS_IsObject(result)) {
                const char* keys[3] = {"x", "y", "z"};
                for (int i = 0; i < count; ++i)
                    if (!readComponent(ctx, result, keys[i], parsed[i])) return false;
            } else {
                return false;
            }
            for (int i = 0; i < count; ++i) value.vec[i] = parsed[i];
            return true;
        }
    }
    return false;
}

constexpr int kMaxConsecutiveErrors = 3;
constexpr double kLoadBudgetMs = 500.0;
constexpr double kCallBudgetMs = 20.0;

std::string buildModuleSource(const std::string& source, int id) {
    std::string text = source;
    text += "\n;globalThis.__lweExports[" + std::to_string(id) + "] = {";
    for (const char* hook : kHooks) {
        text += std::string(hook) + ": typeof " + hook + " === 'function' ? " + hook + " : undefined,";
    }
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
    JSValue context_object = JS_UNDEFINED;  // selected as thisLayer/thisObject owner while this script runs
    int consecutive_errors = 0;
    bool disabled = false;
    uint32_t layer_id = 0;

    JSContext* ctx() const {
        return ScriptEngine::instance().context();
    }

    // Calls `fn` (an exported hook) under this script's error and time budget. The result is owned by the caller.
    bool call(JSValueConst fn, const char* what, int argc, JSValue* argv, JSValue& result, double budget_ms) {
        result = JS_UNDEFINED;
        JSContext* c = ctx();
        if (!c || disabled || !JS_IsFunction(c, fn)) return false;

        JSValue global = JS_GetGlobalObject(c);
        JS_SetPropertyStr(c, global, "__lweCurrent", JS_DupValue(c, context_object));
        JS_FreeValue(c, global);

        ScriptEngine::CallScope scope(ScriptEngine::instance(), &errors, id, budget_ms);
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
        return JS_GetPropertyStr(c, exports, name);  // owned by the caller
    }
};

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
    const std::string name = "script://" + std::to_string(impl_->id);
    const std::string module = buildModuleSource(source, impl_->id);

    ScriptEngine::CallScope scope(engine, &impl_->errors, impl_->id, kLoadBudgetMs);
    JSValue promise = JS_Eval(ctx, module.c_str(), module.size(), name.c_str(), JS_EVAL_TYPE_MODULE);
    if (JS_IsException(promise)) {
        scriptLogException(ctx, "compile");
        return false;
    }

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
        impl_->context_object = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, impl_->context_object, "id", JS_NewInt32(ctx, impl_->id));
        JS_SetPropertyStr(ctx, impl_->context_object, "layerId", JS_NewUint32(ctx, impl_->layer_id));
    }
    JS_FreeValue(ctx, global);
    return loaded;
}

bool SceneScript::callInit(double& value) {
    if (!impl_ || !impl_->retained || !JS_IsFunction(impl_->ctx(), impl_->init_fn)) return false;
    JSContext* ctx = impl_->ctx();
    JSValue argument = JS_NewFloat64(ctx, value);
    JSValue result;
    bool ok = impl_->call(impl_->init_fn, "init", 1, &argument, result, kLoadBudgetMs);
    JS_FreeValue(ctx, argument);
    double number = 0.0;
    ok = ok && JS_IsNumber(result) && JS_ToFloat64(ctx, &number, result) == 0 && number == number;
    JS_FreeValue(ctx, result);
    if (ok) value = number;
    return ok;
}

bool SceneScript::updateNumber(double value, double& out) {
    if (!valid()) return false;
    JSContext* ctx = impl_->ctx();
    JSValue argument = JS_NewFloat64(ctx, value);
    JSValue result;
    bool ok = impl_->call(impl_->update_fn, "update", 1, &argument, result, kCallBudgetMs);
    JS_FreeValue(ctx, argument);
    double number = 0.0;
    ok = ok && JS_IsNumber(result) && JS_ToFloat64(ctx, &number, result) == 0 && number == number;
    JS_FreeValue(ctx, result);
    if (ok) out = number;
    return ok;
}

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

ScriptValue ScriptValue::makeVec3(double x, double y, double z) {
    ScriptValue result;
    result.kind = Kind::Vec3;
    result.vec[0] = x;
    result.vec[1] = y;
    result.vec[2] = z;
    return result;
}

ScriptValue ScriptValue::makeVec2(double x, double y) {
    ScriptValue result;
    result.kind = Kind::Vec2;
    result.vec[0] = x;
    result.vec[1] = y;
    return result;
}

ScriptValue ScriptValue::makeJson(std::string json) {
    ScriptValue result;
    result.kind = Kind::Json;
    result.text = std::move(json);
    return result;
}

uint32_t SceneScript::layerId() const {
    return impl_ ? impl_->layer_id : 0;
}

bool SceneScript::callHook(const char* name, const ScriptEvent& event, bool with_event) {
    if (!impl_ || !impl_->retained) return false;
    JSContext* ctx = impl_->ctx();
    JSValue fn = impl_->hook(name);
    bool ok = false;
    if (JS_IsFunction(ctx, fn)) {
        JSValue argument = JS_UNDEFINED;
        if (with_event || !event.empty()) {
            argument = JS_NewObject(ctx);
            for (const auto& [key, value] : event) JS_SetPropertyStr(ctx, argument, key.c_str(), toJsValue(ctx, value));
        }
        JSValue result;
        ok = impl_->call(fn, name, JS_IsUndefined(argument) ? 0 : 1, &argument, result, kCallBudgetMs);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, argument);
    }
    JS_FreeValue(ctx, fn);
    return ok;
}

ScriptValue ScriptValue::makeString(std::string value) {
    ScriptValue result;
    result.kind = Kind::String;
    result.text = std::move(value);
    return result;
}

void SceneScript::setLayerId(uint32_t layer_id) {
    if (!impl_) return;
    impl_->layer_id = layer_id;
    if (impl_->retained && JS_IsObject(impl_->context_object))
        JS_SetPropertyStr(impl_->ctx(), impl_->context_object, "layerId", JS_NewUint32(impl_->ctx(), layer_id));
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

bool SceneScript::update(const std::string& value, std::string& out) {
    if (!valid()) return false;
    JSContext* ctx = impl_->ctx();
    JSValue argument = JS_NewStringLen(ctx, value.c_str(), value.size());
    JSValue result;
    bool ok = impl_->call(impl_->update_fn, "update", 1, &argument, result, kCallBudgetMs);
    JS_FreeValue(ctx, argument);
    if (ok && !JS_IsUndefined(result)) {
        if (const char* text = JS_ToCString(ctx, result)) {
            out = text;
            JS_FreeCString(ctx, text);
        }
    } else {
        ok = ok && !JS_IsUndefined(result);
    }
    JS_FreeValue(ctx, result);
    return ok;
}

void SceneScript::callWithString(const std::string& function, const std::string& argument) {
    if (!impl_ || !impl_->retained) return;
    JSValue fn = impl_->hook(function.c_str());
    JSContext* ctx = impl_->ctx();
    JSValue value = JS_NewStringLen(ctx, argument.c_str(), argument.size());
    JSValue result;
    impl_->call(fn, function.c_str(), 1, &value, result, kCallBudgetMs);
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, fn);
}

void SceneScript::mediaPropertiesChanged(const std::string& title) {
    if (!impl_ || !impl_->retained) return;
    JSContext* ctx = impl_->ctx();
    JSValue fn = impl_->hook("mediaPropertiesChanged");
    JSValue event = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, event, "title", JS_NewStringLen(ctx, title.c_str(), title.size()));
    JSValue result;
    impl_->call(fn, "mediaPropertiesChanged", 1, &event, result, kCallBudgetMs);
    JS_FreeValue(ctx, event);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, fn);
}
