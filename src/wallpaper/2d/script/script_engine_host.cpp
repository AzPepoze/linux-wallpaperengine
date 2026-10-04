#include <quickjs.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

enum class ListKind { Floats, Indices };

// The numbers of a Float32Array / Uint16Array / Uint32Array, or of a plain array.
bool numberList(JSContext* ctx, JSValueConst value, ListKind kind, std::vector<double>& out) {
    size_t offset = 0, byte_length = 0, bytes = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &byte_length, &bytes);
    if (!JS_IsException(buffer)) {
        size_t buffer_size = 0;
        const uint8_t* data = JS_GetArrayBuffer(ctx, &buffer_size, buffer);
        JS_FreeValue(ctx, buffer);
        if (!data || offset + byte_length > buffer_size || bytes == 0) return false;
        data += offset;
        for (size_t i = 0; i < byte_length / bytes; ++i) {
            const uint8_t* element = data + i * bytes;
            double number = 0.0;
            if (kind == ListKind::Floats && bytes == 4) {
                float f;
                memcpy(&f, element, 4);
                number = f;
            } else if (kind == ListKind::Floats && bytes == 8) {
                memcpy(&number, element, 8);
            } else if (bytes == 4) {
                uint32_t u;
                memcpy(&u, element, 4);
                number = u;
            } else if (bytes == 2) {
                uint16_t u;
                memcpy(&u, element, 2);
                number = u;
            } else {
                number = *element;
            }
            out.push_back(number);
        }
        return true;
    }
    JS_FreeValue(ctx, JS_GetException(ctx));
    if (!JS_IsArray(ctx, value)) return false;
    JSValue length = JS_GetPropertyStr(ctx, value, "length");
    uint32_t count = 0;
    JS_ToUint32(ctx, &count, length);
    JS_FreeValue(ctx, length);
    for (uint32_t i = 0; i < count; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, value, i);
        double number = 0.0;
        JS_ToFloat64(ctx, &number, item);
        JS_FreeValue(ctx, item);
        out.push_back(number);
    }
    return true;
}

bool stringProperty(JSContext* ctx, JSValueConst object, const char* name, std::string& out) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    const char* text = JS_IsString(value) ? JS_ToCString(ctx, value) : nullptr;
    if (text) {
        out = text;
        JS_FreeCString(ctx, text);
    }
    JS_FreeValue(ctx, value);
    return text != nullptr;
}

// One shape of createModelData / applyData / replaceData.
bool parseShapePatch(JSContext* ctx, JSValueConst shape, int index, ShapePatch& patch) {
    patch.index = index;
    if (JS_IsNull(shape)) {
        patch.remove = true;
        return true;
    }
    if (!JS_IsObject(shape)) return false;

    JSValue vertices = JS_GetPropertyStr(ctx, shape, "vertexBuffer");
    if (!JS_IsUndefined(vertices) && !JS_IsNull(vertices)) {
        std::vector<double> numbers;
        if (numberList(ctx, vertices, ListKind::Floats, numbers)) {
            patch.has_vertices = true;
            patch.data.vertices.assign(numbers.begin(), numbers.end());
        }
    }
    JS_FreeValue(ctx, vertices);

    JSValue format = JS_GetPropertyStr(ctx, shape, "vertexFormat");
    if (JS_IsArray(ctx, format)) {
        patch.has_format = true;
        JSValue length = JS_GetPropertyStr(ctx, format, "length");
        uint32_t count = 0;
        JS_ToUint32(ctx, &count, length);
        JS_FreeValue(ctx, length);
        for (uint32_t i = 0; i < count; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, format, i);
            const char* name = JS_IsString(item) ? JS_ToCString(ctx, item) : nullptr;
            if (name) {
                const std::string attribute = name;
                if (attribute == "position") patch.data.format.push_back(VertexAttribute::Position);
                if (attribute == "normal") patch.data.format.push_back(VertexAttribute::Normal);
                if (attribute == "uv") patch.data.format.push_back(VertexAttribute::Uv);
                if (attribute == "tangent_signed") patch.data.format.push_back(VertexAttribute::TangentSigned);
                if (attribute == "color") patch.data.format.push_back(VertexAttribute::Color);
                JS_FreeCString(ctx, name);
            }
            JS_FreeValue(ctx, item);
        }
    }
    JS_FreeValue(ctx, format);

    JSValue indices = JS_GetPropertyStr(ctx, shape, "indexBuffer");
    if (JS_IsNull(indices)) {
        patch.remove_indices = true;
    } else if (!JS_IsUndefined(indices)) {
        std::vector<double> numbers;
        if (numberList(ctx, indices, ListKind::Indices, numbers)) {
            patch.has_indices = true;
            patch.data.indices.assign(numbers.begin(), numbers.end());
        }
    }
    JS_FreeValue(ctx, indices);

    JSValue material = JS_GetPropertyStr(ctx, shape, "material");
    if (JS_IsString(material)) {
        const char* text = JS_ToCString(ctx, material);
        if (text) {
            patch.has_material = true;
            patch.data.material = text;
            JS_FreeCString(ctx, text);
        }
    } else if (JS_IsObject(material)) {
        // An IAssetHandle from engine.registerAsset().
        patch.has_material = stringProperty(ctx, material, "file", patch.data.material);
    }
    JS_FreeValue(ctx, material);

    JSValue origin = JS_GetPropertyStr(ctx, shape, "origin");
    if (JS_IsObject(origin)) {
        patch.has_origin = true;
        const char* axes[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            JSValue axis = JS_GetPropertyStr(ctx, origin, axes[i]);
            double number = 0.0;
            if (!JS_IsUndefined(axis)) JS_ToFloat64(ctx, &number, axis);
            patch.data.origin[i] = (float)number;
            JS_FreeValue(ctx, axis);
        }
    }
    JS_FreeValue(ctx, origin);
    return true;
}

// `shapes` is one shape object or an array of them (a null entry removes that shape).
std::vector<ShapePatch> parseShapePatches(JSContext* ctx, JSValueConst shapes) {
    std::vector<ShapePatch> patches;
    if (JS_IsArray(ctx, shapes)) {
        JSValue length = JS_GetPropertyStr(ctx, shapes, "length");
        uint32_t count = 0;
        JS_ToUint32(ctx, &count, length);
        JS_FreeValue(ctx, length);
        for (uint32_t i = 0; i < count; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, shapes, i);
            ShapePatch patch;
            if (parseShapePatch(ctx, item, (int)i, patch)) patches.push_back(std::move(patch));
            JS_FreeValue(ctx, item);
        }
    } else {
        ShapePatch patch;
        if (parseShapePatch(ctx, shapes, 0, patch)) patches.push_back(std::move(patch));
    }
    return patches;
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

    if (op == "sceneGet") {
        std::vector<double> values;
        if (!scene->getSceneProperty(stringArg(1), values)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, values[i]));
        return array;
    }
    if (op == "sceneSet" && argc > 2) {
        std::vector<double> values;
        JSValue length = JS_GetPropertyStr(ctx, argv[2], "length");
        uint32_t count = 0;
        JS_ToUint32(ctx, &count, length);
        JS_FreeValue(ctx, length);
        for (uint32_t i = 0; i < count && i < 4; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[2], i);
            double number = 0.0;
            JS_ToFloat64(ctx, &number, item);
            JS_FreeValue(ctx, item);
            values.push_back(number);
        }
        return JS_NewBool(ctx, scene->setSceneProperty(stringArg(1), values));
    }

    const uint32_t id = idArg(1);
    if (op == "modelCreate" && argc > 1) return JS_NewUint32(ctx, scene->createModelData(parseShapePatches(ctx, argv[1])));
    if (op == "modelUpdate" && argc > 3)
        return JS_NewBool(ctx, scene->updateModelData(id, parseShapePatches(ctx, argv[2]), JS_ToBool(ctx, argv[3]) > 0));
    if (op == "modelDestroy") return JS_NewBool(ctx, scene->destroyModelData(id));
    if (op == "exists") return JS_NewBool(ctx, scene->layerExists(id));
    if (op == "createLayer") return JS_NewUint32(ctx, scene->createLayer(stringArg(1)));
    if (op == "destroyLayer") return JS_NewBool(ctx, scene->destroyLayer(id));
    if (op == "sortLayer") return JS_NewBool(ctx, scene->sortLayer(id, (int)idArg(2)));
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
    if (op == "setParent")
        return JS_NewBool(ctx, scene->setParent(id, idArg(2), stringArg(3), argc > 4 && JS_ToBool(ctx, argv[4]) > 0));
    if (op == "rotateObjectSpace" && argc > 2) {
        double angles[3] = {0.0, 0.0, 0.0};
        for (uint32_t i = 0; i < 3; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[2], i);
            JS_ToFloat64(ctx, &angles[i], item);
            JS_FreeValue(ctx, item);
        }
        return JS_NewBool(ctx, scene->rotateObjectSpace(id, angles));
    }
    if (op == "attachToTexture") {
        std::vector<double> values;
        if (!scene->transformAttachmentToTexture(id, idArg(2), stringArg(3), values)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, values[i]));
        return array;
    }
    if (op == "attachIndex") return JS_NewInt32(ctx, scene->findAttachment(id, stringArg(2)));
    if (op == "attachGet") {
        std::vector<double> values;
        if (!scene->getAttachment(id, stringArg(2), stringArg(3), values)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, values[i]));
        return array;
    }
    if (op == "animLayerCreate") return JS_NewUint32(ctx, scene->createAnimationLayer(id, stringArg(2)));
    if (op == "animLayerDestroy") return JS_NewBool(ctx, scene->destroyAnimationLayer(id, stringArg(2)));
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

    if (op == "initialConfig") return JS_NewString(ctx, scene->initialLayerConfig(id).c_str());
    if (op == "boneCount") return JS_NewInt32(ctx, scene->boneCount(id));
    if (op == "boneFind") return JS_NewInt32(ctx, scene->findBone(id, stringArg(2)));
    if (op == "boneName") return JS_NewString(ctx, scene->boneName(id, (int)idArg(2)).c_str());
    if (op == "boneParent") return JS_NewInt32(ctx, scene->boneParent(id, (int)idArg(2)));
    if (op == "boneReset") return JS_NewBool(ctx, scene->resetBone(id, (int)idArg(2)));
    if (op == "boneGet") {
        std::vector<double> values;
        if (!scene->getBone(id, (int)idArg(2), stringArg(3), values)) return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, values[i]));
        return array;
    }
    if (op == "boneSet" && argc > 4) {
        std::vector<double> values;
        uint32_t count = 0;
        JSValue length = JS_GetPropertyStr(ctx, argv[4], "length");
        JS_ToUint32(ctx, &count, length);
        JS_FreeValue(ctx, length);
        for (uint32_t i = 0; i < count && i < 16; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[4], i);
            double number = 0.0;
            JS_ToFloat64(ctx, &number, item);
            JS_FreeValue(ctx, item);
            values.push_back(number);
        }
        return JS_NewBool(ctx, scene->setBone(id, (int)idArg(2), stringArg(3), values));
    }
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
    if (op == "effPasses") return JS_NewInt32(ctx, scene->effectPassCount(id, (int)idArg(2)));
    if (op == "effExec") return JS_NewBool(ctx, scene->executeMaterialFunction(id, (int)idArg(2), stringArg(3)));
    if (op == "passGet") {
        std::vector<double> values;
        if (!scene->getPassMaterialProperty(id, (int)idArg(2), (int)idArg(3), stringArg(4), values))
            return JS_UNDEFINED;
        JSValue array = JS_NewArray(ctx);
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, JS_NewFloat64(ctx, values[i]));
        return array;
    }
    if (op == "passSet" && argc > 5) {
        std::vector<double> values;
        JSValue length = JS_GetPropertyStr(ctx, argv[5], "length");
        uint32_t count = 0;
        JS_ToUint32(ctx, &count, length);
        JS_FreeValue(ctx, length);
        for (uint32_t i = 0; i < count && i < 4; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[5], i);
            double number = 0.0;
            JS_ToFloat64(ctx, &number, item);
            JS_FreeValue(ctx, item);
            values.push_back(number);
        }
        return JS_NewBool(ctx, scene->setPassMaterialProperty(id, (int)idArg(2), (int)idArg(3), stringArg(4), values));
    }
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
