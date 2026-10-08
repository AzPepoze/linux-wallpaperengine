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
                // The animation of the constant this script drives (`effect:<i>:<constant>`).
                current.thisObject.getAnimation = function (name) {
                    var key = name === undefined ? current.property : String(name);
                    var id = __lweScene('animFind', current.layerId, 'timeline', key);
                    if (!id) id = __lweScene('animFind', current.layerId, 'timeline', current.property || '');
                    return animationHandle(id);
                };
                return current.thisObject;
            }
            current.thisObject = {
                getAnimation: function (name) {
                    var key = name === undefined ? (current.property || '') : String(name);
                    var id = __lweScene('animFind', current.layerId, 'any', key);
                    if (!id) id = __lweScene('animFind', current.layerId, 'any', '');
                    return animationHandle(id);
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

// Fallbacks for a missing assets folder; `shared` is per scene so transitions don't share state.
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
    verticalalign: scalarProperty('verticalalign', String),
    anchor: scalarProperty('anchor', String),
    padding: scalarProperty('padding', Number),
    opaquebackground: scalarProperty('opaquebackground', Boolean),
    limitrows: scalarProperty('limitrows', Boolean),
    limitwidth: scalarProperty('limitwidth', Boolean),
    backgroundcolor: vectorProperty('backgroundcolor', 3),
    solid: scalarProperty('solid', Boolean)
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
// An empty handle (id 0) reads as undefined and no-ops, like Wallpaper Engine's animation object for a layer without one.
var emptyAnimation = null;
function animationHandle(id) {
    if (!id) return emptyAnimation || (emptyAnimation = new AnimationHandle(0));
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
EffectHandle.prototype.getMaterialCount = function () { return __lweScene('effPasses', this.__layer, this.__index); };
EffectHandle.prototype.executeMaterialFunction = function (name) {
    __lweScene('effExec', this.__layer, this.__index, String(name));
};
// An IMaterial has no fixed members: every shader constant of its pass reads and writes as a property.
EffectHandle.prototype.getMaterial = function (index) {
    var layer = this.__layer, effect = this.__index, pass = Number(index) || 0;
    if (pass < 0 || pass >= __lweScene('effPasses', layer, effect)) return undefined;
    return new Proxy({}, {
        get: function (target, name) {
            if (typeof name !== 'string') return undefined;
            return materialResult(__lweScene('passGet', layer, effect, pass, name));
        },
        set: function (target, name, value) {
            if (typeof name === 'string') __lweScene('passSet', layer, effect, pass, name, materialValues(value));
            return true;
        }
    });
};
EffectHandle.prototype.getMaterialProperty = function (name) {
    return materialResult(__lweScene('matGet', this.__layer, this.__index, String(name)));
};
EffectHandle.prototype.setMaterialProperty = function (name, value) {
    return __lweScene('matSet', this.__layer, this.__index, String(name), materialValues(value));
};
function effectHandle(layerId, index) { return new EffectHandle(layerId, index); }

// Bones are addressed by index or name.
function boneIndexOf(layer, bone) {
    return typeof bone === 'number' ? bone : __lweScene('boneFind', layer.__id, String(bone));
}
LayerHandle.prototype.getBoneCount = function () { return __lweScene('boneCount', this.__id); };
LayerHandle.prototype.getBoneIndex = function (name) { return __lweScene('boneFind', this.__id, String(name)); };
LayerHandle.prototype.getBoneParentIndex = function (bone) {
    return __lweScene('boneParent', this.__id, boneIndexOf(this, bone));
};
function matrixOf(value) {
    return value && Array.isArray(value.m) && value.m.length === 16 ? value.m : (Array.isArray(value) ? value : undefined);
}
[['getBoneTransform', 'setBoneTransform', 'matrix'], ['getLocalBoneTransform', 'setLocalBoneTransform', 'localmatrix']]
    .forEach(function (names) {
        LayerHandle.prototype[names[0]] = function (bone) {
            var m = __lweScene('boneGet', this.__id, boneIndexOf(this, bone), names[2]);
            return m && typeof g.Mat4 === 'function' ? new g.Mat4(m) : undefined;
        };
        LayerHandle.prototype[names[1]] = function (bone, transform) {
            var m = matrixOf(transform);
            if (m) __lweScene('boneSet', this.__id, boneIndexOf(this, bone), names[2], m);
        };
    });
['Origin', 'Angles'].forEach(function (what) {
    var field = what.toLowerCase();
    LayerHandle.prototype['getLocalBone' + what] = function (bone) {
        var a = __lweScene('boneGet', this.__id, boneIndexOf(this, bone), field);
        return a ? vec3(a[0], a[1], a[2]) : undefined;
    };
    LayerHandle.prototype['setLocalBone' + what] = function (bone, value) {
        __lweScene('boneSet', this.__id, boneIndexOf(this, bone), field, toArray(value, 3));
    };
});
// No model in the supported formats carries blend shapes or physics bones, so these answer for a model without them.
LayerHandle.prototype.getBlendShapeIndex = function () { return -1; };
LayerHandle.prototype.getBlendShapeWeight = function () { return 0; };
LayerHandle.prototype.setBlendShapeWeight = function () {};
LayerHandle.prototype.applyBonePhysicsImpulse = function () {};
LayerHandle.prototype.resetBonePhysicsSimulation = function () {};

// IVideoTexture rides on the animation handle plumbing: `__id` is the handle of the layer's video.
function VideoTextureHandle(handle) { Object.defineProperty(this, '__id', { value: handle }); }
Object.defineProperties(VideoTextureHandle.prototype, {
    duration: animationNumber('duration'),
    rate: animationNumber('rate', true),
    loop: {
        get: function () { var v = __lweScene('animGet', this.__id, 'loop'); return v === undefined ? undefined : v !== 0; },
        set: function (value) { __lweScene('animSet', this.__id, 'loop', value ? 1 : 0); }, enumerable: true
    }
});
['play', 'pause', 'stop'].forEach(function (command) {
    VideoTextureHandle.prototype[command] = function () { __lweScene('animCommand', this.__id, command); };
});
VideoTextureHandle.prototype.isPlaying = function () { return __lweScene('animGet', this.__id, 'playing') === 1; };
VideoTextureHandle.prototype.getCurrentTime = function () { return __lweScene('animGet', this.__id, 'currentTime'); };
VideoTextureHandle.prototype.setCurrentTime = function (time) {
    __lweScene('animSet', this.__id, 'currentTime', Number(time));
};
VideoTextureHandle.prototype.addEndedCallback = function (callback) {
    (endedCallbacks[this.__id] = endedCallbacks[this.__id] || []).push(callback);
};
LayerHandle.prototype.getVideoTexture = function () {
    var handle = __lweScene('animFind', this.__id, 'video', '');
    return handle ? new VideoTextureHandle(handle) : undefined;
};

function ParticleInstanceHandle(layerId) { Object.defineProperty(this, '__id', { value: layerId }); }
(function () {
    var props = {};
    ['alpha', 'size', 'count', 'speed', 'lifetime', 'rate', 'colorn'].forEach(function (field) {
        props[field] = scalarProperty('particle.' + field, Number);
    });
    for (var i = 0; i < 8; ++i) props['controlpoint' + i] = vectorProperty('particle.controlpoint' + i, 3);
    Object.defineProperties(ParticleInstanceHandle.prototype, props);
})();
function ParticleHandle(layerId) {
    Object.defineProperty(this, '__id', { value: layerId });
    Object.defineProperty(this, 'instance', { value: new ParticleInstanceHandle(layerId), enumerable: true });
}
['play', 'pause', 'stop'].forEach(function (command) {
    ParticleHandle.prototype[command] = function () { __lweScene('layerCommand', this.__id, 'particle.' + command); };
});
ParticleHandle.prototype.isPlaying = function () { return __lweScene('get', this.__id, 'particle.playing') === true; };
ParticleHandle.prototype.emitParticles = function (count) {
    __lweScene('layerCommand', this.__id, 'particle.emit:' + (count === undefined ? 1 : Number(count) | 0));
};
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
// setParent(parent, adjustTransforms?) or setParent(parent, attachment, adjustTransform?)
LayerHandle.prototype.setParent = function (parent, second, third) {
    var handle = parent && typeof parent === 'object' ? parent : (parent !== undefined && parent !== null && parent !== ''
                                                                  ? thisScene.getLayer(parent) : undefined);
    var attachment = typeof second === 'string' || typeof second === 'number' ? String(second) : '';
    var adjust = typeof second === 'boolean' ? second : !!third;
    return __lweScene('setParent', this.__id, handle ? handle.__id : 0, attachment, adjust);
};
LayerHandle.prototype.rotateObjectSpace = function (angles) {
    return __lweScene('rotateObjectSpace', this.__id, toArray(angles, 3));
};
LayerHandle.prototype.transformAttachmentToTexture = function (attachmentLayer, attachmentName) {
    var other = attachmentLayer && typeof attachmentLayer === 'object' ? attachmentLayer
                                                                        : thisScene.getLayer(attachmentLayer);
    if (!other) return undefined;
    var m = __lweScene('attachToTexture', this.__id, other.__id, String(attachmentName));
    return m && typeof g.Mat3 === 'function' ? new g.Mat3(m) : undefined;
};
LayerHandle.prototype.getAttachmentIndex =function (name) { return __lweScene('attachIndex', this.__id, String(name)); };
LayerHandle.prototype.getAttachmentMatrix = function (attachment) {
    var m = __lweScene('attachGet', this.__id, String(attachment), 'matrix');
    return m && typeof g.Mat4 === 'function' ? new g.Mat4(m) : undefined;
};
['Origin', 'Angles'].forEach(function (what) {
    LayerHandle.prototype['getAttachment' + what] = function (attachment) {
        var a = __lweScene('attachGet', this.__id, String(attachment), what.toLowerCase());
        return a ? vec3(a[0], a[1], a[2]) : undefined;
    };
});
// IModelLayer
Object.defineProperties(LayerHandle.prototype, {
    rootmotion: {
        get: function () { return __lweScene('get', this.__id, 'rootmotion'); },
        set: function (value) { __lweScene('set', this.__id, 'rootmotion', !!value); }, enumerable: true
    },
    perspective: {
        get: function () { return __lweScene('get', this.__id, 'perspective'); },
        set: function (value) { __lweScene('set', this.__id, 'perspective', !!value); }, enumerable: true
    }
});
function animationConfig(animation, extra) {
    var config = typeof animation === 'object' && animation !== null ? Object.assign({}, animation) : { animation: animation };
    if (typeof animation === 'object' && animation !== null && config.animation === undefined) config.animation = config.name;
    return Object.assign(config, extra || {});
}
LayerHandle.prototype.createAnimationLayer = function (animation) {
    return animationHandle(__lweScene('animLayerCreate', this.__id, JSON.stringify(animationConfig(animation))));
};
LayerHandle.prototype.playSingleAnimation = function (animation, config) {
    return animationHandle(__lweScene('animLayerCreate', this.__id, JSON.stringify(
        animationConfig(animation, Object.assign({}, config, { once: true, autoRemove: true })))));
};
LayerHandle.prototype.destroyAnimationLayer = function (animationLayer) {
    var key = typeof animationLayer === 'object' && animationLayer !== null ? animationLayer.name : animationLayer;
    return __lweScene('animLayerDestroy', this.__id, String(key));
};
LayerHandle.prototype.getChildren =function () { return __lweScene('children', this.__id).map(layerHandle); };
function layerHandle(id) {
    if (!id || !__lweScene('exists', id)) return undefined;
    return handles[id] || (handles[id] = new LayerHandle(id));
}
// IModelData: custom geometry for a layer made with thisScene.createLayer({ model }).
hide('IModelData', {
    POSITION: 'position', NORMAL: 'normal', UV: 'uv', TANGENT_SIGNED: 'tangent_signed', COLOR: 'color'
});
function ModelDataHandle(id) { Object.defineProperty(this, '__id', { value: id }); }
// A shape is one object, or an array of them (null removes a shape in replaceData).
ModelDataHandle.prototype.applyData = function (shapes) { __lweScene('modelUpdate', this.__id, shapes, false); };
ModelDataHandle.prototype.replaceData = function (shapes) { __lweScene('modelUpdate', this.__id, shapes, true); };
hide('thisScene', {
    getLayer: function (nameOrIndex) { return layerHandle(__lweScene('find', nameOrIndex)); },
    getLayerCount: function () { return __lweScene('list').length; },
    enumerateLayers: function () { return __lweScene('list').map(layerHandle); },
    getLayerIndex: function (layer) {
        var handle = typeof layer === 'object' ? layer : this.getLayer(layer);
        return handle ? __lweScene('index', handle.__id) : -1;
    },
    // The 2D renderer is orthographic, so the camera vectors are kept for scripts but do not move the view.
    getCameraTransforms: function () {
        var read = function (name) {
            var a = __lweScene('sceneGet', name);
            return a ? vec3(a[0], a[1], a[2]) : undefined;
        };
        var zoom = __lweScene('sceneGet', 'camerazoom');
        return { eye: read('cameraeye'), center: read('cameracenter'), up: read('cameraup'),
                 zoom: zoom ? zoom[0] : undefined };
    },
    setCameraTransforms: function (transforms) {
        if (!transforms) return;
        if (typeof transforms.zoom === 'number') __lweScene('sceneSet', 'camerazoom', [transforms.zoom]);
        ['eye', 'center', 'up'].forEach(function (key) {
            if (transforms[key]) __lweScene('sceneSet', 'camera' + key, toArray(transforms[key], 3));
        });
    },
    createModelData: function (configuration) {
        var id = configuration && configuration.shapes ? __lweScene('modelCreate', configuration.shapes) : 0;
        return id ? new ModelDataHandle(id) : undefined;
    },
    destroyModelData: function (modelData) {
        if (modelData instanceof ModelDataHandle) __lweScene('modelDestroy', modelData.__id);
    },
    getInitialLayerConfig: function (layer) {
        var handle = typeof layer === 'object' ? layer : this.getLayer(layer);
        var json = handle ? __lweScene('initialConfig', handle.__id) : '';
        try { return json ? JSON.parse(json) : undefined; } catch (e) { return undefined; }
    },
    // `config` is an asset path or an object shaped like an entry of scene.json `objects`.
    createLayer: function (config) {
        var json;
        // An asset handle from engine.registerAsset() stands for the asset's path.
        if (config && typeof config === 'object' && typeof config.file === 'string' && Object.keys(config).length === 1)
            config = config.file;
        try {
            // scene.json writes vectors as "x y z"; model data goes by its id.
            json = JSON.stringify(config, function (key, value) {
                if (key === 'model' && value instanceof ModelDataHandle) return value.__id;
                if (value && typeof value === 'object' && typeof value.x === 'number' && typeof value.y === 'number')
                    return [value.x, value.y, value.z].filter(function (v) { return v !== undefined; }).join(' ');
                return value;
            });
        } catch (e) { return undefined; }
        return layerHandle(__lweScene('createLayer', json));
    },
    destroyLayer: function (layer) {
        var handle = typeof layer === 'object' ? layer : this.getLayer(layer);
        return !!handle && __lweScene('destroyLayer', handle.__id);
    },
    sortLayer: function (layer, index) {
        var handle = typeof layer === 'object' ? layer : this.getLayer(layer);
        return !!handle && __lweScene('sortLayer', handle.__id, Number(index) | 0);
    }
});
(function () {
    var kinds = {
        bloom: 'bool', cameraparallax: 'bool', camerashake: 'bool', clearenabled: 'bool', camerafade: 'bool',
        fov: 'number', nearz: 'number', farz: 'number',
        clearcolor: 'vec3', ambientcolor: 'vec3', skylightcolor: 'vec3',
        bloomstrength: 'number', bloomthreshold: 'number', cameraparallaxamount: 'number',
        cameraparallaxdelay: 'number', cameraparallaxmouseinfluence: 'number',
        camerashakeamplitude: 'number', camerashakespeed: 'number', camerashakeroughness: 'number'
    };
    Object.keys(kinds).forEach(function (name) {
        var kind = kinds[name];
        Object.defineProperty(g.thisScene, name, {
            enumerable: true,
            get: function () {
                var a = __lweScene('sceneGet', name);
                if (!a) return undefined;
                if (kind === 'bool') return a[0] !== 0;
                if (kind === 'vec3') return vec3(a[0], a[1], a[2]);
                return a[0];
            },
            set: function (value) {
                __lweScene('sceneSet', name, kind === 'vec3' ? toArray(value, 3) : [Number(value)]);
            }
        });
    });
})();
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
// Overrides apply after the module runs and touch only declared properties.
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
