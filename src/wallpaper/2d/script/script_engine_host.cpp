#include <quickjs.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "script_engine.h"
#include "script_engine_internal.h"
#include "script_scene_backend.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

std::string readScriptFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

namespace {

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
    const int64_t now = scriptNowNs();
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
        assets.empty() ? std::string() : readScriptFile(assets + "/scripts/jsmodules/" + lower + ".js");
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

}  // namespace

void installScriptHostFunctions(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__lweLog", JS_NewCFunction(ctx, jsLog, "__lweLog", 2));
    JS_SetPropertyStr(ctx, global, "__lweStorageLoad", JS_NewCFunction(ctx, jsStorageLoad, "__lweStorageLoad", 1));
    JS_SetPropertyStr(ctx, global, "__lweStorageSave", JS_NewCFunction(ctx, jsStorageSave, "__lweStorageSave", 2));
    JS_SetPropertyStr(ctx, global, "__lweScene", JS_NewCFunction(ctx, jsScene, "__lweScene", 4));
    JS_FreeValue(ctx, global);
}

void installScriptModuleLoader(JSRuntime* runtime) {
    JS_SetModuleLoaderFunc(runtime, nullptr, moduleLoader, nullptr);
}
