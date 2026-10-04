#include "script_engine_internal.h"

namespace {

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

hide('__lweCurrent', { id: 0, layerId: 0, object: undefined });
Object.defineProperty(g, 'thisLayer', { get: function () { return layerHandle(g.__lweCurrent.layerId); }, configurable: true });
Object.defineProperty(g, 'thisObject', {
    get: function () {
        var current = g.__lweCurrent;
        if (!current.layerId) return undefined;
        if (!current.thisObject) {
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

}  // namespace

const char* scriptPreludeSource() {
    return kPrelude;
}
