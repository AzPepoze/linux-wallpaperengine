#ifndef SCRIPT_VALUE_JS_H
#define SCRIPT_VALUE_JS_H

#include <quickjs.h>

#include "scene_script.h"

// Vec2 / Vec3 values become instances of the install's classes (plain {x, y, z} objects without them).
JSValue toJsValue(JSContext* ctx, const ScriptValue& value);

// Replaces `value` with `result` when it has a compatible type; a number broadcasts into a vector.
bool fromJsValue(JSContext* ctx, JSValueConst result, ScriptValue& value);

// An object with one property per event field.
JSValue toJsObject(JSContext* ctx, const ScriptEvent& event);

#endif  // SCRIPT_VALUE_JS_H
