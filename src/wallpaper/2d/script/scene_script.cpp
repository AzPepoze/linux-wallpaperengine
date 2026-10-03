#include "scene_script.h"

#include <quickjs.h>

#include <cstring>

#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {
// Wallpaper Engine property builder + the globals text scripts expect. The
// builder records each declared property's default; `finish()` returns the
// object a script stores as `scriptProperties`.
constexpr const char* kPrelude = R"JS(
var __lweScriptProperties = {};
var engine = {
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

// Merges the scene's `scriptproperties` over the script-declared defaults,
// unwrapping the `{ user, value }` form WE uses for user-bound properties.
constexpr const char* kMergeOverrides =
    "if (typeof scriptProperties === 'object' && scriptProperties !== null && typeof __lweOverrides === 'object') {\n"
    "  Object.keys(__lweOverrides).forEach(function (key) {\n"
    "    var v = __lweOverrides[key];\n"
    "    if (v !== null && typeof v === 'object' && 'value' in v) v = v.value;\n"
    "    scriptProperties[key] = v;\n"
    "  });\n"
    "}\n";

// Removes the ES module `export` keywords so the script evaluates in the global
// scope, where `update` / `mediaPropertiesChanged` can be called directly.
std::string stripModuleSyntax(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    size_t i = 0;
    while (i < source.size()) {
        if (source.compare(i, 7, "export ") == 0) {
            i += 7;
            continue;
        }
        out.push_back(source[i++]);
    }
    return out;
}

void logException(JSContext* ctx, const char* what) {
    JSValue exception = JS_GetException(ctx);
    const char* message = JS_ToCString(ctx, exception);
    LOG_TAG_W(TAG, "SceneScript %s error: %s", what, message ? message : "(unknown)");
    if (message) JS_FreeCString(ctx, message);
    JS_FreeValue(ctx, exception);
}
}  // namespace

struct SceneScript::Impl {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    bool has_update = false;
};

SceneScript::SceneScript() : impl_(std::make_unique<Impl>()) {}

SceneScript::~SceneScript() {
    if (!impl_) return;
    if (impl_->context) JS_FreeContext(impl_->context);
    if (impl_->runtime) JS_FreeRuntime(impl_->runtime);
}

bool SceneScript::valid() const { return impl_ && impl_->context != nullptr && impl_->has_update; }

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
