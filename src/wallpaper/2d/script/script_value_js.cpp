#include "script_value_js.h"

namespace {

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

}  // namespace

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
            if (JS_IsNumber(result)) {  // Wallpaper Engine broadcasts a number to every component
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

JSValue toJsObject(JSContext* ctx, const ScriptEvent& event) {
    JSValue object = JS_NewObject(ctx);
    for (const auto& [key, value] : event) JS_SetPropertyStr(ctx, object, key.c_str(), toJsValue(ctx, value));
    return object;
}
