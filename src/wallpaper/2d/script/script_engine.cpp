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
#include "script_value_js.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr const char* kPrelude = R"JS(
(function () {
'use strict';
var g = globalThis;
function hide(name, value) {
    Object.defineProperty(g, name, { value: value, writable: true, configurable: true, enumerable: false });
}
function vec2(x, y) { return typeof g.Vec2 === 'function' ? new g.Vec2(x, y) : { x: x, y: y }; }

// `class Vec3 {}` in baseclasses.js is a global lexical binding, not a property of globalThis; expose them as properties.
['Vec2', 'Vec3', 'Vec4', 'Mat3', 'Mat4', 'MediaPlaybackEvent'].forEach(function (name) {
    if (Object.prototype.hasOwnProperty.call(g, name)) return;
    try { hide(name, (0, eval)(name)); } catch (e) { /* baseclasses.js not loaded */ }
});

hide('__lweCurrent', { id: 0, layerId: 0, scopeKey: 0, object: undefined });
Object.defineProperty(g, 'thisLayer', { get: function () { return layerHandle(g.__lweCurrent.layerId); }, configurable: true });
Object.defineProperty(g, 'thisObject', {
    get: function () {
        var current = g.__lweCurrent;
        if (!current.layerId) return undefined;
        if (!current.thisObject) {
            var effect = /^effect:(\d+):/.exec(current.property || '');
            if (effect) {
                current.thisObject = effectHandle(current.layerId, Number(effect[1]));
                return current.thisObject;
            }
            current.thisObject = {
                getAnimation: function (name) {
                    return animationHandle(__lweScene('animFind', current.layerId, 'any',
                                                      name === undefined ? (current.property || '') : String(name)));
                }
            };
            Object.defineProperties(current.thisObject, {
                visible: {
                    get: function () { return __lweScene('get', current.layerId, 'visible'); },
                    set: function (value) { __lweScene('set', current.layerId, 'visible', !!value); },
                    enumerable: true
                },
                name: { get: function () { return __lweScene('name', current.layerId); }, enumerable: true }
            });
        }
        return current.thisObject;
    },
    configurable: true
});

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

// Fallbacks for a missing assets folder; baseclasses.js defines the real ones.
// `shared` is one object per scene so two wallpapers alive in a transition do not see each other's state.
var sharedByScope = {};
delete g.shared;
Object.defineProperty(g, 'shared', {
    get: function () {
        var key = g.__lweCurrent.scopeKey || 0;
        return sharedByScope[key] || (sharedByScope[key] = {});
    },
    configurable: true
});
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
hide('__lweSetUserProperties', function (properties) { engine.userProperties = properties; });

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
function scopeKey() { return g.__lweCurrent.scopeKey || 0; }
function slot(location) { return scopeKey() + ':' + areaKey(location); }
function area(location) {
    var key = slot(location);
    if (!store[key]) {
        try { store[key] = JSON.parse(__lweStorageLoad(scopeKey(), areaKey(location)) || '{}'); } catch (e) { store[key] = {}; }
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
        dirty[slot(location)] = true;
    },
    delete: function (key, location) {
        var data = area(location);
        var had = Object.prototype.hasOwnProperty.call(data, key);
        delete data[key];
        if (had) dirty[slot(location)] = true;
        return had;
    },
    clear: function (location) {
        store[slot(location)] = {};
        dirty[slot(location)] = true;
    }
});
hide('__lweStorageFlush', function () {
    Object.keys(dirty).forEach(function (key) {
        var parts = key.split(':');
        __lweStorageSave(Number(parts[0]), parts[1], JSON.stringify(store[key]));
    });
    dirty = {};
});

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
function scalarProperty(name, Type) {
    return {
        get: function () { return __lweScene('get', this.__id, name); },
        set: function (value) { __lweScene('set', this.__id, name, Type(value)); },
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
    name: { get: function () { return __lweScene('name', this.__id); }, enumerable: true },
    color: vectorProperty('color', 3),
    alpha: scalarProperty('alpha', Number),
    text: scalarProperty('text', String),
    font: scalarProperty('font', String),
    pointsize: scalarProperty('pointsize', Number),
    maxwidth: scalarProperty('maxwidth', Number),
    maxrows: scalarProperty('maxrows', Number),
    horizontalalign: scalarProperty('horizontalalign', String),
    verticalalign: scalarProperty('verticalalign', String)
});
var animationHandles = {};
var endedCallbacks = {};
function AnimationHandle(id) { Object.defineProperty(this, '__id', { value: id }); }
function animationNumber(name, writable) {
    var property = { get: function () { return __lweScene('animGet', this.__id, name); }, enumerable: true };
    if (writable) property.set = function (value) { __lweScene('animSet', this.__id, name, Number(value)); };
    return property;
}
Object.defineProperties(AnimationHandle.prototype, {
    rate: animationNumber('rate', true),
    fps: animationNumber('fps'),
    frameCount: animationNumber('frameCount'),
    duration: animationNumber('duration'),
    blend: animationNumber('blend', true),
    visible: {
        get: function () { var v = __lweScene('animGet', this.__id, 'visible'); return v === undefined ? undefined : v !== 0; },
        set: function (value) { __lweScene('animSet', this.__id, 'visible', value ? 1 : 0); }, enumerable: true
    },
    name: { get: function () { return __lweScene('animGet', this.__id, 'name'); }, enumerable: true }
});
AnimationHandle.prototype.join = function () { __lweScene('animCommand', this.__id, 'join'); };
AnimationHandle.prototype.play = function () { __lweScene('animCommand', this.__id, 'play'); };
AnimationHandle.prototype.stop = function () { __lweScene('animCommand', this.__id, 'stop'); };
AnimationHandle.prototype.pause = function () { __lweScene('animCommand', this.__id, 'pause'); };
AnimationHandle.prototype.isPlaying = function () { return __lweScene('animGet', this.__id, 'playing') === 1; };
AnimationHandle.prototype.getFrame = function () { return __lweScene('animGet', this.__id, 'frame'); };
AnimationHandle.prototype.setFrame = function (frame) { __lweScene('animSet', this.__id, 'frame', Number(frame)); };
AnimationHandle.prototype.addEndedCallback = function (callback) {
    (endedCallbacks[this.__id] = endedCallbacks[this.__id] || []).push(callback);
};
function animationHandle(id) {
    if (!id) return undefined;
    return animationHandles[id] || (animationHandles[id] = new AnimationHandle(id));
}
hide('__lweAnimationEnded', function (id) {
    (endedCallbacks[id] || []).slice().forEach(function (callback) {
        try { callback(); } catch (e) { __lweLog(1, 'animation ended callback: ' + e); }
    });
});
LayerHandle.prototype.getAnimation = function (name) {
    return animationHandle(__lweScene('animFind', this.__id, 'any', name === undefined ? '' : String(name)));
};
LayerHandle.prototype.getTextureAnimation = function () {
    return animationHandle(__lweScene('animFind', this.__id, 'texture', ''));
};
LayerHandle.prototype.getAnimationLayer = function (nameOrIndex) {
    return animationHandle(__lweScene('animFind', this.__id, 'layer', String(nameOrIndex)));
};
LayerHandle.prototype.getAnimationLayerCount = function () { return __lweScene('animCount', this.__id); };

function materialValues(value) {
    if (typeof value === 'number' || typeof value === 'boolean') return [Number(value)];
    if (Array.isArray(value)) return value.map(Number);
    if (value && typeof value === 'object') {
        return ['x', 'y', 'z', 'w'].filter(function (k) { return typeof value[k] === 'number'; })
                                   .map(function (k) { return value[k]; });
    }
    return [];
}
function materialResult(values) {
    if (!values) return undefined;
    if (values.length === 1) return values[0];
    if (values.length === 2) return vec2(values[0], values[1]);
    if (values.length === 3 && typeof g.Vec3 === 'function') return new g.Vec3(values[0], values[1], values[2]);
    if (values.length === 4 && typeof g.Vec4 === 'function') return new g.Vec4(values[0], values[1], values[2], values[3]);
    return values;
}
function EffectHandle(layerId, index) {
    Object.defineProperty(this, '__layer', { value: layerId });
    Object.defineProperty(this, '__index', { value: index });
}
Object.defineProperties(EffectHandle.prototype, {
    visible: {
        get: function () { return __lweScene('effVisible', this.__layer, this.__index); },
        set: function (value) { __lweScene('effSetVisible', this.__layer, this.__index, !!value); }, enumerable: true
    },
    name: { get: function () { return __lweScene('effName', this.__layer, this.__index); }, enumerable: true }
});
EffectHandle.prototype.getMaterial = function () { return this; };
EffectHandle.prototype.getMaterialProperty = function (name) {
    return materialResult(__lweScene('matGet', this.__layer, this.__index, String(name)));
};
EffectHandle.prototype.setMaterialProperty = function (name, value) {
    return __lweScene('matSet', this.__layer, this.__index, String(name), materialValues(value));
};
function effectHandle(layerId, index) { return new EffectHandle(layerId, index); }

function ParticleHandle(layerId) { Object.defineProperty(this, '__id', { value: layerId }); }
(function () {
    var props = { color: vectorProperty('particle.color', 3) };
    ['alpha', 'size', 'count', 'speed', 'lifetime', 'rate'].forEach(function (field) {
        props[field] = scalarProperty('particle.' + field, Number);
    });
    for (var i = 0; i < 8; ++i) props['controlpoint' + i] = vectorProperty('particle.controlpoint' + i, 3);
    Object.defineProperties(ParticleHandle.prototype, props);
})();
['play', 'pause', 'stop'].forEach(function (command) {
    ParticleHandle.prototype[command] = function () { __lweScene('layerCommand', this.__id, 'particle.' + command); };
});
ParticleHandle.prototype.emitParticles = function (count) {
    __lweScene('layerCommand', this.__id, 'particle.emit:' + (Number(count) | 0));
};
ParticleHandle.prototype.getInstanceCount = function () { return 1; };
ParticleHandle.prototype.getInstance = function () { return this; };
LayerHandle.prototype.getParticleSystem = function () {
    return __lweScene('get', this.__id, 'particle.rate') === undefined ? undefined : new ParticleHandle(this.__id);
};
LayerHandle.prototype.getEffectCount = function () { return __lweScene('effCount', this.__id); };
LayerHandle.prototype.getEffect = function (nameOrIndex) {
    var index = typeof nameOrIndex === 'number' ? nameOrIndex : __lweScene('effFind', this.__id, String(nameOrIndex));
    return index >= 0 && index < __lweScene('effCount', this.__id) ? effectHandle(this.__id, index) : undefined;
};
['play', 'stop', 'pause'].forEach(function (command) {
    LayerHandle.prototype[command] = function () { __lweScene('layerCommand', this.__id, command); };
});
LayerHandle.prototype.isPlaying = function () { return __lweScene('get', this.__id, 'playing') === true; };
Object.defineProperty(LayerHandle.prototype, 'volume', scalarProperty('volume', Number));
LayerHandle.prototype.getTransformMatrix = function () {
    var m = __lweScene('matrix', this.__id);
    return m && typeof g.Mat4 === 'function' ? new g.Mat4(m) : undefined;
};
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
// Overrides are applied after the module ran and only touch declared properties, like the real engine's
// _Internal.updateScriptProperties.
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

// __lweStorageLoad(scopeKey, area) / __lweStorageSave(scopeKey, area, json)
JSValue jsStorageLoad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int32_t scope_key = 0;
    if (argc > 0) JS_ToInt32(ctx, &scope_key, argv[0]);
    const char* key = argc > 1 ? JS_ToCString(ctx, argv[1]) : nullptr;
    if (!key) return JS_NewString(ctx, "");
    std::ifstream file(storagePath(ScriptEngine::instance().wallpaperIdForKey(scope_key), key, false));
    JS_FreeCString(ctx, key);
    std::stringstream contents;
    contents << file.rdbuf();
    return JS_NewString(ctx, contents.str().c_str());
}

JSValue jsStorageSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;
    int32_t scope_key = 0;
    JS_ToInt32(ctx, &scope_key, argv[0]);
    const char* key = JS_ToCString(ctx, argv[1]);
    const char* json = JS_ToCString(ctx, argv[2]);
    if (key && json) {
        std::ofstream file(storagePath(ScriptEngine::instance().wallpaperIdForKey(scope_key), key, true),
                           std::ios::trunc);
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

JSValue idArray(JSContext* ctx, const std::vector<uint32_t>& ids) {
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < ids.size(); ++i) JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewUint32(ctx, ids[i]));
    return array;
}

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
    if (op == "layerCommand") return JS_NewBool(ctx, scene->layerCommand(id, stringArg(2)));
    if (op == "name") return JS_NewString(ctx, scene->layerName(id).c_str());
    if (op == "matrix") {
        double m[16];
        if (!scene->getWorldMatrix(id, m)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (uint32_t i = 0; i < 16; ++i) JS_SetPropertyUint32(ctx, array, i, JS_NewFloat64(ctx, m[i]));
        return array;
    }
    if (op == "parent") return JS_NewUint32(ctx, scene->parentOf(id));
    if (op == "children") return idArray(ctx, scene->childrenOf(id));
    if (op == "index") {
        const std::vector<uint32_t> layers = scene->allLayers();
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i] == id) return JS_NewInt32(ctx, (int32_t)i);
        return JS_NewInt32(ctx, -1);
    }

    if (op == "animFind") return JS_NewUint32(ctx, scene->findAnimation(id, stringArg(2), stringArg(3)));
    if (op == "animCount") return JS_NewInt32(ctx, scene->animationLayerCount(id));
    if (op == "animGet") {
        const std::string field = stringArg(2);
        double number = 0.0;
        if (scene->animationGet(id, field, number)) return JS_NewFloat64(ctx, number);
        std::string text;
        if (scene->animationGetString(id, field, text)) return JS_NewString(ctx, text.c_str());
        return JS_UNDEFINED;
    }
    if (op == "animSet" && argc > 3) {
        double number = 0.0;
        JS_ToFloat64(ctx, &number, argv[3]);
        return JS_NewBool(ctx, scene->animationSet(id, stringArg(2), number));
    }
    if (op == "animCommand") return JS_NewBool(ctx, scene->animationCommand(id, stringArg(2)));

    if (op == "effCount") return JS_NewInt32(ctx, scene->effectCount(id));
    if (op == "effFind") return JS_NewInt32(ctx, scene->findEffect(id, stringArg(2)));
    if (op == "effName") return JS_NewString(ctx, scene->effectName(id, (int)idArg(2)).c_str());
    if (op == "effVisible") {
        bool visible = false;
        if (!scene->effectVisible(id, (int)idArg(2), visible)) return JS_UNDEFINED;
        return JS_NewBool(ctx, visible);
    }
    if (op == "effSetVisible" && argc > 3)
        return JS_NewBool(ctx, scene->setEffectVisible(id, (int)idArg(2), JS_ToBool(ctx, argv[3]) != 0));
    if (op == "matGet") {
        std::vector<double> values;
        if (!scene->getMaterialProperty(id, (int)idArg(2), stringArg(3), values)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, values[i]));
        return array;
    }
    if (op == "matSet" && argc > 4) {
        std::vector<double> values;
        JSValue length = JS_GetPropertyStr(ctx, argv[4], "length");
        uint32_t count = 0;
        JS_ToUint32(ctx, &count, length);
        JS_FreeValue(ctx, length);
        for (uint32_t i = 0; i < count && i < 4; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[4], i);
            double number = 0.0;
            JS_ToFloat64(ctx, &number, item);
            JS_FreeValue(ctx, item);
            values.push_back(number);
        }
        return JS_NewBool(ctx, scene->setMaterialProperty(id, (int)idArg(2), stringArg(3), values));
    }

    const std::string property = stringArg(2);
    if (op == "get") {
        bool flag = false;
        if (scene->getBool(id, property, flag)) return JS_NewBool(ctx, flag);
        double number = 0.0;
        if (scene->getNumber(id, property, number)) return JS_NewFloat64(ctx, number);
        std::string text;
        if (scene->getString(id, property, text)) return JS_NewStringLen(ctx, text.c_str(), text.size());
        double v[3] = {0.0, 0.0, 0.0};
        int components = 0;
        if (!scene->getVector(id, property, v, components)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (int i = 0; i < components; ++i) JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, v[i]));
        return array;
    }
    if (op == "set" && argc > 3) {
        if (JS_IsBool(argv[3])) return JS_NewBool(ctx, scene->setBool(id, property, JS_ToBool(ctx, argv[3]) != 0));
        if (JS_IsNumber(argv[3])) {
            double number = 0.0;
            JS_ToFloat64(ctx, &number, argv[3]);
            return JS_NewBool(ctx, scene->setNumber(id, property, number));
        }
        if (JS_IsString(argv[3])) {
            const char* text = JS_ToCString(ctx, argv[3]);
            const bool ok = text && scene->setString(id, property, text);
            if (text) JS_FreeCString(ctx, text);
            return JS_NewBool(ctx, ok);
        }
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
        if (entry.script->scope() != active_scope_ || !entry.script->callHook(hook, event)) continue;
        ++delivered;
    }
    return delivered;
}

int ScriptEngine::dispatchToLayer(uint32_t layer_id, const char* hook, const ScriptEvent& event) {
    int delivered = 0;
    const std::vector<ScriptEntry> entries = scripts_;
    for (const ScriptEntry& entry : entries)
        if (entry.script->scope() == active_scope_ && entry.script->layerId() == layer_id &&
            entry.script->callHook(hook, event))
            ++delivered;
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
    for (const ScriptEntry& entry : scripts_) {
        if (entry.script->scope() != active_scope_) continue;
        for (const char* hook : hooks)
            if (entry.script->hasFunction(hook)) return true;
    }
    return false;
}

std::vector<uint32_t> ScriptEngine::layersWithHooks(const std::vector<const char*>& hooks) {
    std::vector<uint32_t> ids;
    for (const ScriptEntry& entry : scripts_) {
        if (entry.script->layerId() == 0 || entry.script->scope() != active_scope_) continue;
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
    : CallScope(engine, errors, script_id, budget_ms, engine.active_scope_) {}

ScriptEngine::CallScope::CallScope(ScriptEngine& engine, ScriptErrors* errors, int script_id, double budget_ms,
                                   const void* scope)
    : engine_(engine),
      previous_errors_(engine.current_errors_),
      previous_id_(engine.current_script_id_),
      previous_scope_(engine.current_scope_) {
    engine.current_errors_ = errors;
    engine.current_script_id_ = script_id;
    engine.current_scope_ = scope;
    engine.deadline_ns_ = nowNs() + (int64_t)(budget_ms * 1e6);
}

ScriptEngine::CallScope::~CallScope() {
    engine_.current_errors_ = previous_errors_;
    engine_.current_script_id_ = previous_id_;
    engine_.current_scope_ = previous_scope_;
    engine_.deadline_ns_ = 0;
}

void ScriptEngine::registerScope(const void* scope, ScriptSceneBackend* backend, const std::string& wallpaper_id) {
    ScopeInfo& info = scopes_[scope];
    if (info.key == 0 && scope != nullptr) info.key = ++next_scope_key_;
    info.backend = backend;
    info.wallpaper_id = wallpaper_id;
}

int ScriptEngine::scopeKey(const void* scope) const {
    const auto it = scopes_.find(scope);
    return it == scopes_.end() ? 0 : it->second.key;
}

std::string ScriptEngine::wallpaperIdForKey(int key) const {
    for (const auto& [scope, info] : scopes_)
        if (info.key == key && !info.wallpaper_id.empty()) return info.wallpaper_id;
    return wallpaper_id_;
}

void ScriptEngine::unregisterScope(const void* scope) {
    scopes_.erase(scope);
    if (active_scope_ == scope) active_scope_ = nullptr;
    if (creation_scope_ == scope) creation_scope_ = nullptr;
}

ScriptSceneBackend* ScriptEngine::sceneBackend() const {
    const auto it = scopes_.find(current_scope_);
    return it == scopes_.end() ? nullptr : it->second.backend;
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
        if (scripts_[i].sticky_delivered || scripts_[i].script->scope() != active_scope_) continue;
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
