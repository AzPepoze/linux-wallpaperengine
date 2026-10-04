#include "script_engine.h"

#include <quickjs.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "script_scene_backend.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Globals Wallpaper Engine scripts expect. Everything here is plain JS; the few natives it calls are registered in
// ScriptEngine::create(). Per-script state (thisLayer/thisObject) is selected through `__lweCurrent`.
constexpr const char* kPrelude = R"JS(
(function () {
'use strict';
var g = globalThis;
function hide(name, value) {
    Object.defineProperty(g, name, { value: value, writable: true, configurable: true, enumerable: false });
}
function vec2(x, y) { return typeof g.Vec2 === 'function' ? new g.Vec2(x, y) : { x: x, y: y }; }

// `class Vec3 {}` in baseclasses.js is a global lexical binding, not a property of globalThis, so the host (and the
// helpers below) could not find it. Expose the classes as properties; scripts still resolve the same objects.
['Vec2', 'Vec3', 'Vec4', 'Mat3', 'Mat4', 'MediaPlaybackEvent'].forEach(function (name) {
    if (Object.prototype.hasOwnProperty.call(g, name)) return;
    try { hide(name, (0, eval)(name)); } catch (e) { /* baseclasses.js not loaded */ }
});

hide('__lweCurrent', { id: 0, layerId: 0, object: undefined });
Object.defineProperty(g, 'thisLayer', { get: function () { return layerHandle(g.__lweCurrent.layerId); }, configurable: true });
Object.defineProperty(g, 'thisObject', { get: function () { return g.__lweCurrent.object; }, configurable: true });

function format(args) {
    return Array.prototype.map.call(args, function (v) {
        if (typeof v === 'string') return v;
        if (v !== null && typeof v === 'object' && v.toString === Object.prototype.toString) {
            try { return JSON.stringify(v); } catch (e) { return String(v); }
        }
        return String(v);
    }).join(' ');
}
hide('console', {
    log: function () { __lweLog(0, format(arguments)); },
    info: function () { __lweLog(0, format(arguments)); },
    debug: function () { __lweLog(0, format(arguments)); },
    warn: function () { __lweLog(1, format(arguments)); },
    error: function () { __lweLog(1, format(arguments)); }
});

// baseclasses.js from the Wallpaper Engine install defines these; the fallbacks only cover a missing assets folder.
if (typeof g.shared === 'undefined') hide('shared', {});
if (typeof g.createScriptProperties !== 'function') {
    hide('createScriptProperties', function () {
        var vars = {};
        var builder = new Proxy({}, { get: function (target, key) {
            if (key === 'finish') return function () { return vars; };
            if (typeof key === 'string' && key.indexOf('add') === 0)
                return function (options) { if (options && options.name !== undefined) vars[options.name] = options.value; return builder; };
            return undefined;
        } });
        return builder;
    });
}

var audio = {};
[16, 32, 64].forEach(function (n) {
    audio[n] = { average: new Float32Array(n), left: new Float32Array(n), right: new Float32Array(n) };
});
hide('__lweAudio', audio);

var clock = { runtime: 0 };
var timers = [];
function addTimer(callback, delay, repeat) {
    var ms = Math.max(0, Number(delay) || 0);
    var timer = { callback: callback, due: clock.runtime + ms / 1000, interval: repeat ? Math.max(1, ms) / 1000 : 0,
                  dead: false };
    timers.push(timer);
    return function () { timer.dead = true; };
}

var engine = {
    AUDIO_RESOLUTION_16: 16, AUDIO_RESOLUTION_32: 32, AUDIO_RESOLUTION_64: 64,
    frametime: 1 / 60, runtime: 0, timeOfDay: 0,
    canvasSize: vec2(1920, 1080), screenResolution: vec2(1920, 1080), userProperties: {},
    isDesktopDevice: function () { return true; },
    isMobileDevice: function () { return false; },
    isWallpaper: function () { return true; },
    isScreensaver: function () { return false; },
    isRunningInEditor: function () { return false; },
    isPortrait: function () { return engine.screenResolution.y > engine.screenResolution.x; },
    isLandscape: function () { return engine.screenResolution.x >= engine.screenResolution.y; },
    openUserShortcut: function () { return false; },
    registerAudioBuffers: function (resolution) { return audio[resolution] || audio[16]; },
    registerAsset: function (file) { return { file: String(file) }; },
    setTimeout: function (callback, delay) { return addTimer(callback, delay, false); },
    setInterval: function (callback, delay) { return addTimer(callback, delay, true); }
};
hide('engine', engine);

hide('__lweTick', function (dt, runtime, canvasW, canvasH, screenW, screenH) {
    engine.frametime = dt;
    engine.runtime = runtime;
    clock.runtime = runtime;
    if (engine.canvasSize.x !== canvasW || engine.canvasSize.y !== canvasH) engine.canvasSize = vec2(canvasW, canvasH);
    if (engine.screenResolution.x !== screenW || engine.screenResolution.y !== screenH)
        engine.screenResolution = vec2(screenW, screenH);
    var now = new Date();
    engine.timeOfDay = (now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds()) / 86400;
    for (var i = 0; i < timers.length;) {
        var timer = timers[i];
        if (timer.dead) { timers.splice(i, 1); continue; }
        if (clock.runtime >= timer.due) {
            if (timer.interval > 0) {
                timer.due += timer.interval;
                if (timer.due <= clock.runtime) timer.due = clock.runtime + timer.interval;
            } else {
                timer.dead = true;
            }
            try { timer.callback(); } catch (e) { __lweLog(1, 'timer callback: ' + e); }
        }
        i++;
    }
});

var store = {};
var dirty = {};
function areaKey(location) { return location === 'global' ? 'global' : 'screen'; }
function area(location) {
    var key = areaKey(location);
    if (!store[key]) {
        try { store[key] = JSON.parse(__lweStorageLoad(key) || '{}'); } catch (e) { store[key] = {}; }
    }
    return store[key];
}
hide('localStorage', {
    LOCATION_GLOBAL: 'global', LOCATION_SCREEN: 'screen',
    get: function (key, location) {
        var data = area(location);
        return Object.prototype.hasOwnProperty.call(data, key) ? data[key] : undefined;
    },
    set: function (key, value, location) {
        var data = area(location);
        var previous = data[key];
        data[key] = value;
        if (JSON.stringify(data).length > 102400) {
            if (previous === undefined) delete data[key]; else data[key] = previous;
            __lweLog(1, 'localStorage: 100 KB limit reached, value for "' + key + '" not stored');
            return;
        }
        dirty[areaKey(location)] = true;
    },
    delete: function (key, location) {
        var data = area(location);
        var had = Object.prototype.hasOwnProperty.call(data, key);
        delete data[key];
        if (had) dirty[areaKey(location)] = true;
        return had;
    },
    clear: function (location) {
        store[areaKey(location)] = {};
        dirty[areaKey(location)] = true;
    }
});
hide('__lweStorageFlush', function () {
    Object.keys(dirty).forEach(function (key) { __lweStorageSave(key, JSON.stringify(store[key])); });
    dirty = {};
});

// Layer handles: thin objects over the scene backend, one per layer id, so the same layer is always the same object.
function toArray(value, count) {
    if (typeof value === 'number') return count === 2 ? [value, value] : [value, value, value];
    return count === 2 ? [value.x, value.y] : [value.x, value.y, value.z];
}
function vectorProperty(name, count) {
    var Ctor = function () { return count === 2 ? g.Vec2 : g.Vec3; };
    return {
        get: function () {
            var a = __lweScene('get', this.__id, name);
            if (!a) return undefined;
            var C = Ctor();
            return C ? (count === 2 ? new C(a[0], a[1]) : new C(a[0], a[1], a[2])) : { x: a[0], y: a[1], z: a[2] };
        },
        set: function (value) { __lweScene('set', this.__id, name, toArray(value, count)); },
        enumerable: true
    };
}
var handles = {};
function LayerHandle(id) { Object.defineProperty(this, '__id', { value: id }); }
Object.defineProperties(LayerHandle.prototype, {
    origin: vectorProperty('origin', 3),
    scale: vectorProperty('scale', 3),
    angles: vectorProperty('angles', 3),
    parallaxDepth: vectorProperty('parallaxDepth', 2),
    size: vectorProperty('size', 2),
    visible: {
        get: function () { return __lweScene('get', this.__id, 'visible'); },
        set: function (value) { __lweScene('set', this.__id, 'visible', !!value); }, enumerable: true
    },
    name: { get: function () { return __lweScene('name', this.__id); }, enumerable: true }
});
LayerHandle.prototype.getParent = function () { return layerHandle(__lweScene('parent', this.__id)); };
LayerHandle.prototype.getChildren = function () { return __lweScene('children', this.__id).map(layerHandle); };
function layerHandle(id) {
    if (!id || !__lweScene('exists', id)) return undefined;
    return handles[id] || (handles[id] = new LayerHandle(id));
}
hide('thisScene', {
    getLayer: function (nameOrIndex) { return layerHandle(__lweScene('find', nameOrIndex)); },
    getLayerCount: function () { return __lweScene('list').length; },
    enumerateLayers: function () { return __lweScene('list').map(layerHandle); },
    getLayerIndex: function (layer) {
        var handle = typeof layer === 'object' ? layer : this.getLayer(layer);
        return handle ? __lweScene('index', handle.__id) : -1;
    },
    createLayer: function () { return null; }, destroyLayer: function () { return false; },
    sortLayer: function () { return false; }
});
function vec3(x, y, z) { return typeof g.Vec3 === 'function' ? new g.Vec3(x, y, z) : { x: x, y: y, z: z }; }
var inputState = { cursorWorldPosition: vec3(0, 0, 0), cursorScreenPosition: vec2(0, 0), cursorLeftDown: false };
hide('input', inputState);
hide('__lweSetInput', function (worldX, worldY, screenX, screenY, leftDown) {
    inputState.cursorWorldPosition = vec3(worldX, worldY, 0);
    inputState.cursorScreenPosition = vec2(screenX, screenY);
    inputState.cursorLeftDown = leftDown;
});

hide('__lweExports', {});
hide('__lweErrors', {});
hide('__lweWatch', function (promise, id) {
    promise.then(null, function (reason) { g.__lweErrors[id] = String(reason); });
});
// Scene `scriptproperties` overrides are applied after the module ran, the way the engine does it
// (baseclasses.js `_Internal.updateScriptProperties` only touches properties the script declared).
hide('__lweApplyOverrides', function (exported, json) {
    if (!exported || !exported.scriptProperties || !json) return;
    var overrides = JSON.parse(json);
    var flat = {};
    Object.keys(overrides).forEach(function (key) {
        var value = overrides[key];
        if (value !== null && typeof value === 'object' && 'value' in value) value = value.value;
        flat[key] = value;
    });
    if (g._Internal && g._Internal.updateScriptProperties) {
        g._Internal.updateScriptProperties(exported, JSON.stringify(flat));
    } else {
        Object.keys(flat).forEach(function (key) {
            if (Object.prototype.hasOwnProperty.call(exported.scriptProperties, key)) exported.scriptProperties[key] = flat[key];
        });
    }
});
})();
)JS";

struct LogBucket {
    int64_t window_start = 0;
    int count = 0;
    int suppressed = 0;
};

JSValue jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int level = 0;
    if (argc > 0) JS_ToInt32(ctx, &level, argv[0]);
    const char* text = argc > 1 ? JS_ToCString(ctx, argv[1]) : nullptr;

    static std::unordered_map<int, LogBucket> buckets;
    const int id = ScriptEngine::instance().currentScriptId();
    LogBucket& bucket = buckets[id];
    const int64_t now = nowNs();
    if (now - bucket.window_start > 1000000000LL) {
        if (bucket.suppressed > 0) LOG_TAG_W(TAG, "script %d: %d console lines suppressed", id, bucket.suppressed);
        bucket = {now, 0, 0};
    }
    if (bucket.count < 20) {
        ++bucket.count;
        if (level >= 1)
            LOG_TAG_W(TAG, "script %d: %s", id, text ? text : "");
        else
            LOG_TAG_I(TAG, "script %d: %s", id, text ? text : "");
    } else {
        ++bucket.suppressed;
    }
    if (text) JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

std::string storagePath(const std::string& wallpaper_id, const std::string& key, bool create_dir) {
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/.local/share/linux-wallpaperengine";
    if (create_dir) {
        mkdir(dir.c_str(), 0755);
        mkdir((dir + "/localstorage").c_str(), 0755);
    }
    std::string safe;
    for (char c : wallpaper_id) safe.push_back(isalnum((unsigned char)c) || c == '-' || c == '_' ? c : '_');
    return dir + "/localstorage/" + safe + "_" + key + ".json";
}

JSValue jsStorageLoad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const char* key = argc > 0 ? JS_ToCString(ctx, argv[0]) : nullptr;
    if (!key) return JS_NewString(ctx, "");
    std::ifstream file(storagePath(ScriptEngine::instance().wallpaperId(), key, false));
    JS_FreeCString(ctx, key);
    std::stringstream contents;
    contents << file.rdbuf();
    return JS_NewString(ctx, contents.str().c_str());
}

JSValue jsStorageSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    const char* key = JS_ToCString(ctx, argv[0]);
    const char* json = JS_ToCString(ctx, argv[1]);
    if (key && json) {
        std::ofstream file(storagePath(ScriptEngine::instance().wallpaperId(), key, true), std::ios::trunc);
        file << json;
    }
    if (key) JS_FreeCString(ctx, key);
    if (json) JS_FreeCString(ctx, json);
    return JS_UNDEFINED;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// `import ... from 'WEMath'` resolves to <assets>/scripts/jsmodules/wemath.js from the Wallpaper Engine install.
JSValue idArray(JSContext* ctx, const std::vector<uint32_t>& ids) {
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < ids.size(); ++i) JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewUint32(ctx, ids[i]));
    return array;
}

// __lweScene(op, ...): the single bridge from the JS layer handles to ScriptSceneBackend.
JSValue jsScene(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    ScriptSceneBackend* scene = ScriptEngine::instance().sceneBackend();
    if (!scene || argc < 1) return JS_UNDEFINED;
    const char* op_text = JS_ToCString(ctx, argv[0]);
    if (!op_text) return JS_UNDEFINED;
    const std::string op = op_text;
    JS_FreeCString(ctx, op_text);

    auto idArg = [&](int index) {
        uint32_t id = 0;
        if (index < argc) JS_ToUint32(ctx, &id, argv[index]);
        return id;
    };
    auto stringArg = [&](int index) {
        std::string text;
        if (index < argc) {
            if (const char* c = JS_ToCString(ctx, argv[index])) {
                text = c;
                JS_FreeCString(ctx, c);
            }
        }
        return text;
    };

    if (op == "list") return idArray(ctx, scene->allLayers());
    if (op == "find") {
        if (argc > 1 && JS_IsNumber(argv[1])) {
            int32_t index = -1;
            JS_ToInt32(ctx, &index, argv[1]);
            const std::vector<uint32_t> layers = scene->allLayers();
            return JS_NewUint32(ctx, index >= 0 && (size_t)index < layers.size() ? layers[(size_t)index] : 0u);
        }
        return JS_NewUint32(ctx, scene->findLayerByName(stringArg(1)));
    }

    const uint32_t id = idArg(1);
    if (op == "exists") return JS_NewBool(ctx, scene->layerExists(id));
    if (op == "name") return JS_NewString(ctx, scene->layerName(id).c_str());
    if (op == "parent") return JS_NewUint32(ctx, scene->parentOf(id));
    if (op == "children") return idArray(ctx, scene->childrenOf(id));
    if (op == "index") {
        const std::vector<uint32_t> layers = scene->allLayers();
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i] == id) return JS_NewInt32(ctx, (int32_t)i);
        return JS_NewInt32(ctx, -1);
    }

    const std::string property = stringArg(2);
    if (op == "get") {
        bool flag = false;
        if (scene->getBool(id, property, flag)) return JS_NewBool(ctx, flag);
        double v[3] = {0.0, 0.0, 0.0};
        int components = 0;
        if (!scene->getVector(id, property, v, components)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (int i = 0; i < components; ++i) JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, v[i]));
        return array;
    }
    if (op == "set" && argc > 3) {
        if (JS_IsBool(argv[3])) return JS_NewBool(ctx, scene->setBool(id, property, JS_ToBool(ctx, argv[3]) != 0));
        double v[3] = {0.0, 0.0, 0.0};
        for (uint32_t i = 0; i < 3; ++i) {
            JSValue component = JS_GetPropertyUint32(ctx, argv[3], i);
            JS_ToFloat64(ctx, &v[i], component);
            JS_FreeValue(ctx, component);
        }
        return JS_NewBool(ctx, scene->setVector(id, property, v));
    }
    return JS_UNDEFINED;
}

JSModuleDef* moduleLoader(JSContext* ctx, const char* name, void*) {
    std::string lower;
    for (const char* c = name; *c; ++c) lower.push_back((char)tolower((unsigned char)*c));
    const std::string& assets = ScriptEngine::instance().assetsDir();
    const std::string source =
        assets.empty() ? std::string() : readFile(assets + "/scripts/jsmodules/" + lower + ".js");
    if (source.empty()) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", name);
        return nullptr;
    }
    JSValue compiled =
        JS_Eval(ctx, source.c_str(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) return nullptr;
    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    JS_FreeValue(ctx, compiled);
    return module;
}

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
    JS_SetModuleLoaderFunc(runtime_, nullptr, moduleLoader, nullptr);
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        LOG_TAG_W(TAG, "QuickJS context creation failed");
        return;
    }

    JSValue global = JS_GetGlobalObject(context_);
    JS_SetPropertyStr(context_, global, "__lweLog", JS_NewCFunction(context_, jsLog, "__lweLog", 2));
    JS_SetPropertyStr(context_, global, "__lweStorageLoad",
                      JS_NewCFunction(context_, jsStorageLoad, "__lweStorageLoad", 1));
    JS_SetPropertyStr(context_, global, "__lweStorageSave",
                      JS_NewCFunction(context_, jsStorageSave, "__lweStorageSave", 2));
    JS_SetPropertyStr(context_, global, "__lweScene", JS_NewCFunction(context_, jsScene, "__lweScene", 4));
    JS_FreeValue(context_, global);

    // The install's own base classes first (Vec2/3/4, Mat3/4, createScriptProperties, shared, MediaPlaybackEvent...).
    std::string base_classes;
    if (!assets_dir_.empty()) base_classes = readFile(assets_dir_ + "/scripts/jsclasses/baseclasses.js");
    if (base_classes.empty())
        LOG_TAG_W(
            TAG, "baseclasses.js not found under the assets folder; Vec2/Vec3/Mat4 and WEMath modules are unavailable");

    struct Chunk {
        const char* name;
        const std::string* text;
    };
    const std::string prelude = kPrelude;
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
    return deadline_ns_ != 0 && nowNs() > deadline_ns_;
}

ScriptEngine::CallScope::CallScope(ScriptEngine& engine, ScriptErrors* errors, int script_id, double budget_ms)
    : engine_(engine), previous_errors_(engine.current_errors_), previous_id_(engine.current_script_id_) {
    engine.current_errors_ = errors;
    engine.current_script_id_ = script_id;
    engine.deadline_ns_ = nowNs() + (int64_t)(budget_ms * 1e6);
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

    // Scripts that loaded after a sticky event (media state...) get the current value once, like on a live scene.
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
