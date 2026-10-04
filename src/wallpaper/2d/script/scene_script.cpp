#include "scene_script.h"

#include <quickjs.h>

#include <cstring>

#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {
// WE property builder + globals text scripts expect; `finish()` yields the
// object a script stores as `scriptProperties`.
constexpr const char* kPrelude = R"JS(
var __lweScriptProperties = {};
var __lweAudioAverage = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
var WEMath = {
    mix: function(a, b, t) { return a * (1 - t) + b * t; },
    clamp: function(v, lo, hi) { return Math.min(Math.max(v, lo), hi); },
    smoothStep: function(a, b, v) {
        var t = Math.min(Math.max((v - a) / (b - a), 0), 1);
        return t * t * (3 - 2 * t);
    },
    deg2rad: function(d) { return d * Math.PI / 180; },
    rad2deg: function(r) { return r * 180 / Math.PI; }
};
var engine = {
    frametime: 0.016,
    registerAudioBuffers: function() {
        return { average: __lweAudioAverage, left: __lweAudioAverage, right: __lweAudioAverage };
    },
    registerAsset: function(path) { return path; },
    openUserShortcut: function() {},
    setTimeout: function() { return 0; },
    clearTimeout: function() {}
};
var thisScene = {
    createLayer: function() { return null; },
    destroyLayer: function() {},
    getLayer: function() { return null; }
};
function createScriptProperties() {
    var builder = {
        addCheckbox: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        addText: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        addSlider: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        addColor: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        addCombo: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        addDirection: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        addFile: function(o) { __lweScriptProperties[o.name] = o.value; return builder; },
        finish: function() { return __lweScriptProperties; }
    };
    return builder;
}
)JS";

// Merges `scriptproperties` over the script defaults, unwrapping WE's
// `{ user, value }` form.
constexpr const char* kMergeOverrides =
    "if (typeof scriptProperties === 'object' && scriptProperties !== null && typeof __lweOverrides === 'object') {\n"
    "  Object.keys(__lweOverrides).forEach(function (key) {\n"
    "    var v = __lweOverrides[key];\n"
    "    if (v !== null && typeof v === 'object' && 'value' in v) v = v.value;\n"
    "    scriptProperties[key] = v;\n"
    "  });\n"
    "}\n";

// Drops the ES module `export` keywords and `import` lines so the script runs in global scope (the imported
// modules, e.g. WEMath, are globals in the prelude).
std::string stripModuleSyntax(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    size_t i = 0;
    while (i < source.size()) {
        const bool line_start = i == 0 || source[i - 1] == '\n';
        if (line_start && source.compare(i, 7, "import ") == 0) {
            const size_t line_end = source.find('\n', i);
            i = line_end == std::string::npos ? source.size() : line_end;
            continue;
        }
        if (source.compare(i, 7, "export ") == 0) {
            i += 7;
            continue;
        }
        out.push_back(source[i++]);
    }
    return out;
}

// Errors seen by one script; reachable from the QuickJS context so the free helpers below can record them.
struct ScriptErrors {
    std::string last;
    int count = 0;
};

void logException(JSContext* ctx, const char* what) {
    JSValue exception = JS_GetException(ctx);
    const char* message = JS_ToCString(ctx, exception);
    LOG_TAG_W(TAG, "SceneScript %s error: %s", what, message ? message : "(unknown)");
    if (auto* errors = static_cast<ScriptErrors*>(JS_GetContextOpaque(ctx))) {
        errors->last = std::string(what) + ": " + (message ? message : "(unknown)");
        ++errors->count;
    }
    if (message) JS_FreeCString(ctx, message);
    JS_FreeValue(ctx, exception);
}
}  // namespace

struct SceneScript::Impl {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    bool has_update = false;
    ScriptErrors errors;
};

SceneScript::SceneScript() : impl_(std::make_unique<Impl>()) {}

SceneScript::~SceneScript() {
    if (!impl_) return;
    if (impl_->context) JS_FreeContext(impl_->context);
    if (impl_->runtime) JS_FreeRuntime(impl_->runtime);
}

const std::string& SceneScript::lastError() const {
    static const std::string none;
    return impl_ ? impl_->errors.last : none;
}

int SceneScript::errorCount() const {
    return impl_ ? impl_->errors.count : 0;
}

bool SceneScript::hasFunction(const char* name) const {
    if (!impl_ || !impl_->context) return false;
    JSContext* ctx = impl_->context;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, name);
    const bool found = JS_IsFunction(ctx, fn);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    return found;
}

bool SceneScript::valid() const {
    return impl_ && impl_->context != nullptr && impl_->has_update;
}

bool SceneScript::load(const std::string& source, const std::string& script_properties_json) {
    if (source.empty() || !impl_ || impl_->context) return false;

    impl_->runtime = JS_NewRuntime();
    if (!impl_->runtime) {
        LOG_TAG_W(TAG, "QuickJS runtime creation failed");
        return false;
    }
    impl_->context = JS_NewContext(impl_->runtime);
    if (!impl_->context) {
        LOG_TAG_W(TAG, "QuickJS context creation failed");
        return false;
    }
    JSContext* ctx = impl_->context;
    JS_SetContextOpaque(ctx, &impl_->errors);

    JSValue prelude = JS_Eval(ctx, kPrelude, strlen(kPrelude), "<scene-script-prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(prelude)) {
        logException(ctx, "prelude");
        JS_FreeValue(ctx, prelude);
        return false;
    }
    JS_FreeValue(ctx, prelude);

    const std::string body = stripModuleSyntax(source);
    JSValue result = JS_Eval(ctx, body.c_str(), body.size(), "<scene-script>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        logException(ctx, "compile");
        JS_FreeValue(ctx, result);
        return false;
    }
    JS_FreeValue(ctx, result);

    if (!script_properties_json.empty()) {
        JSValue overrides =
            JS_ParseJSON(ctx, script_properties_json.c_str(), script_properties_json.size(), "<scriptproperties>");
        if (JS_IsException(overrides)) {
            JS_FreeValue(ctx, overrides);
        } else {
            JSValue global = JS_GetGlobalObject(ctx);
            JS_SetPropertyStr(ctx, global, "__lweOverrides", overrides);  // consumes overrides
            JS_FreeValue(ctx, global);
            JSValue merged =
                JS_Eval(ctx, kMergeOverrides, strlen(kMergeOverrides), "<scriptproperties>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(merged)) logException(ctx, "scriptproperties");
            JS_FreeValue(ctx, merged);
        }
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue update_fn = JS_GetPropertyStr(ctx, global, "update");
    impl_->has_update = JS_IsFunction(ctx, update_fn);
    JS_FreeValue(ctx, update_fn);
    JS_FreeValue(ctx, global);
    return true;
}

namespace {
// Calls global `name` with `argc` numeric arguments already built by the caller; yields its numeric result.
bool callNumeric(JSContext* ctx, const char* name, int argc, JSValue* argv, double& out) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, name);
    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return false;
    }
    JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(result)) {
        logException(ctx, name);
        JS_FreeValue(ctx, result);
        return false;
    }
    double number = 0.0;
    const bool ok = JS_ToFloat64(ctx, &number, result) == 0 && number == number;
    JS_FreeValue(ctx, result);
    if (ok) out = number;
    return ok;
}
}  // namespace

bool SceneScript::callInit(double& out) {
    if (!impl_ || !impl_->context) return false;
    return callNumeric(impl_->context, "init", 0, nullptr, out);
}

bool SceneScript::updateNumber(double value, double& out) {
    if (!impl_ || !impl_->context) return false;
    JSValue argument = JS_NewFloat64(impl_->context, value);
    const bool ok = callNumeric(impl_->context, "update", 1, &argument, out);
    JS_FreeValue(impl_->context, argument);
    return ok;
}

void SceneScript::setFrameTime(double seconds) {
    if (!impl_ || !impl_->context) return;
    JSContext* ctx = impl_->context;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue engine = JS_GetPropertyStr(ctx, global, "engine");
    if (JS_IsObject(engine)) JS_SetPropertyStr(ctx, engine, "frametime", JS_NewFloat64(ctx, seconds));
    JS_FreeValue(ctx, engine);
    JS_FreeValue(ctx, global);
}

void SceneScript::setAudioAverage(const float* bands, int count) {
    if (!impl_ || !impl_->context) return;
    JSContext* ctx = impl_->context;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue array = JS_GetPropertyStr(ctx, global, "__lweAudioAverage");
    if (JS_IsObject(array)) {
        for (int i = 0; i < count; ++i) JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, bands[i]));
    }
    JS_FreeValue(ctx, array);
    JS_FreeValue(ctx, global);
}

bool SceneScript::update(const std::string& value, std::string& out) {
    if (!impl_ || !impl_->has_update || !impl_->context) return false;
    JSContext* ctx = impl_->context;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "update");
    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return false;
    }

    JSValue argument = JS_NewStringLen(ctx, value.c_str(), value.size());
    JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &argument);
    JS_FreeValue(ctx, argument);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(result)) {
        logException(ctx, "update");
        JS_FreeValue(ctx, result);
        return false;
    }
    const char* text = JS_ToCString(ctx, result);
    if (text) {
        out = text;
        JS_FreeCString(ctx, text);
    }
    JS_FreeValue(ctx, result);
    return true;
}

void SceneScript::callWithString(const std::string& function, const std::string& argument) {
    if (!impl_ || !impl_->context) return;
    JSContext* ctx = impl_->context;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, function.c_str());
    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return;
    }
    JSValue value = JS_NewStringLen(ctx, argument.c_str(), argument.size());
    JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &value);
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(result)) logException(ctx, function.c_str());
    JS_FreeValue(ctx, result);
}

void SceneScript::mediaPropertiesChanged(const std::string& title) {
    if (!impl_ || !impl_->context) return;
    JSContext* ctx = impl_->context;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "mediaPropertiesChanged");
    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return;
    }
    JSValue event = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, event, "title", JS_NewStringLen(ctx, title.c_str(), title.size()));
    JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &event);
    JS_FreeValue(ctx, event);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(result)) logException(ctx, "mediaPropertiesChanged");
    JS_FreeValue(ctx, result);
}
