// Script corpus runner: loads every SceneScript block of every scene wallpaper in a Workshop folder, runs
// init() and a few simulated update() frames, and reports compile/runtime errors plus the unsupported-API
// messages they come from. No GPU needed. Use it to measure script support before/after a change.
//
//   xmake build script_corpus
//   bin/debug/script_corpus <workshop-content-dir> [--out report.json] [--work dir] [--ids a,b] [--frames N]
//
// The report and the unpacked packages go to utils/out/ by default (git-ignored).
#include <cjson/cJSON.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "shared/assets/unpack.h"
#include "shared/core/utils.h"
#include "wallpaper/2d/animation/animation_timelines.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/script/scene_script.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/2d/script/script_scene_backend.h"

namespace {

struct ScriptBlock {
    std::string path;  // JSON path of the owning property, e.g. /objects/12/origin
    std::string property;
    std::string source;
    std::string properties_json;
    uint32_t object_id = 0;  // scene object that owns the property (thisLayer), 0 for scene-level scripts
};

// The scene's objects as a script sees them, so thisLayer / thisScene work without a renderer.
struct FakeLayer {
    uint32_t id = 0, parent = 0;
    std::string name;
    double origin[3] = {0, 0, 0}, scale[3] = {1, 1, 1}, angles[3] = {0, 0, 0}, size[2] = {0, 0};
    bool visible = true;
    bool text = false, sound = false, sound_playing = false;
    std::map<std::string, std::string> strings;
    std::map<std::string, double> numbers = {{"alpha", 1.0}};
};

// "x y z" strings, optionally wrapped as { "value": ... } by a script-driven or user-bound property.
void readVector(const cJSON* property, double* out, int count) {
    if (cJSON_IsObject(property)) property = cJSON_GetObjectItemCaseSensitive(property, "value");
    if (!cJSON_IsString(property) || !property->valuestring) return;
    double v[3] = {out[0], out[1], count > 2 ? out[2] : 0.0};
    const int read = sscanf(property->valuestring, "%lf %lf %lf", &v[0], &v[1], &v[2]);
    for (int i = 0; i < count && i < read; ++i) out[i] = v[i];
}

class FakeScene : public ScriptSceneBackend {
   public:
    void load(const cJSON* scene) {
        layers_.clear();
        const cJSON* objects = cJSON_GetObjectItemCaseSensitive(scene, "objects");
        const cJSON* object = nullptr;
        cJSON_ArrayForEach(object, objects) {
            FakeLayer layer;
            const cJSON* id = cJSON_GetObjectItemCaseSensitive(object, "id");
            const cJSON* parent = cJSON_GetObjectItemCaseSensitive(object, "parent");
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(object, "name");
            if (cJSON_IsNumber(id)) layer.id = (uint32_t)id->valuedouble;
            if (cJSON_IsNumber(parent)) layer.parent = (uint32_t)parent->valuedouble;
            if (cJSON_IsString(name) && name->valuestring) layer.name = name->valuestring;
            readVector(cJSON_GetObjectItemCaseSensitive(object, "origin"), layer.origin, 3);
            readVector(cJSON_GetObjectItemCaseSensitive(object, "scale"), layer.scale, 3);
            readVector(cJSON_GetObjectItemCaseSensitive(object, "angles"), layer.angles, 3);
            for (double& angle : layer.angles) angle *= 180.0 / M_PI;  // stored in radians, scripts see degrees
            readVector(cJSON_GetObjectItemCaseSensitive(object, "size"), layer.size, 2);
            const cJSON* visible = cJSON_GetObjectItemCaseSensitive(object, "visible");
            if (cJSON_IsObject(visible)) visible = cJSON_GetObjectItemCaseSensitive(visible, "value");
            if (cJSON_IsBool(visible)) layer.visible = cJSON_IsTrue(visible);
            if (layer.id != 0) layers_.push_back(layer);
        }
    }
    FakeLayer* find(uint32_t id) {
        for (FakeLayer& layer : layers_)
            if (layer.id == id) return &layer;
        return nullptr;
    }

    // Keyframe animations come from the real parser, so scripts see the same timelines as in the engine.
    void loadAnimations(const wallpaper_engine::SceneDocument& document) {
        timelines_ = AnimationTimelines();
        for (const auto& object : document.objects)
            if (object.node.valid && !object.animations.empty()) timelines_.add(object.node.id, object.animations);
    }
    void advance(float dt) {
        timelines_.update(dt, [this](const AnimatedValue& animated) {
            FakeLayer* layer = find(animated.object_id);
            double* target = !layer                          ? nullptr
                             : animated.property == "origin" ? layer->origin
                             : animated.property == "scale"  ? layer->scale
                             : animated.property == "angles" ? layer->angles
                                                             : nullptr;
            if (!target) return;
            const double factor = animated.property == "angles" ? 180.0 / M_PI : 1.0;
            for (int i = 0; i < 3; ++i)
                if (animated.has[i]) target[i] = animated.value[i] * factor;
        });
        for (uint32_t handle : timelines_.takeEnded()) ScriptEngine::instance().animationEnded(handle);
    }
    struct FakeAnimationLayer {
        uint32_t layer_id = 0;
        std::string name;
        double rate = 1, blend = 1, frame = 0;
        bool visible = true, playing = true;
    };
    static constexpr uint32_t kLayerBase = 0x40000000u;

    void loadAnimationLayers(const wallpaper_engine::SceneDocument& document) {
        animation_layers_.clear();
        for (const auto& object : document.objects) {
            for (const auto& entry : object.image.animation_layers) {
                FakeAnimationLayer layer;
                layer.layer_id = object.node.id;
                layer.name = entry.name;
                layer.rate = entry.rate;
                layer.blend = entry.blend;
                layer.visible = entry.visible;
                animation_layers_.push_back(layer);
            }
        }
    }
    FakeAnimationLayer* animationLayer(uint32_t handle) {
        if (handle < kLayerBase || handle - kLayerBase >= animation_layers_.size()) return nullptr;
        return &animation_layers_[handle - kLayerBase];
    }

    uint32_t findAnimation(uint32_t layer_id, const std::string& kind, const std::string& key) override {
        if (kind == "timeline" || kind == "any") {
            if (const uint32_t handle = timelines_.find(layer_id, key)) return handle;
            if (kind == "timeline") return 0;
        }
        if (kind == "layer" || kind == "any") {
            int ordinal = 0;
            for (size_t i = 0; i < animation_layers_.size(); ++i) {
                if (animation_layers_[i].layer_id != layer_id) continue;
                if (animation_layers_[i].name == key || (kind == "layer" && std::to_string(ordinal) == key))
                    return kLayerBase + (uint32_t)i;
                ++ordinal;
            }
        }
        return 0;
    }
    bool animationGet(uint32_t handle, const std::string& field, double& out) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.get(handle, field, out);
        if (field == "rate")
            out = layer->rate;
        else if (field == "blend")
            out = layer->blend;
        else if (field == "visible")
            out = layer->visible ? 1 : 0;
        else if (field == "playing")
            out = layer->playing ? 1 : 0;
        else if (field == "frame")
            out = layer->frame;
        else
            return false;
        return true;
    }
    bool animationGetString(uint32_t handle, const std::string& field, std::string& out) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.getString(handle, field, out);
        if (field != "name") return false;
        out = layer->name;
        return true;
    }
    bool animationSet(uint32_t handle, const std::string& field, double value) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.set(handle, field, value);
        if (field == "rate")
            layer->rate = value;
        else if (field == "blend")
            layer->blend = value;
        else if (field == "visible")
            layer->visible = value != 0;
        else if (field == "frame")
            layer->frame = value;
        else
            return false;
        return true;
    }
    bool animationCommand(uint32_t handle, const std::string& command) override {
        FakeAnimationLayer* layer = animationLayer(handle);
        if (!layer) return timelines_.command(handle, command);
        if (command == "play")
            layer->playing = true;
        else if (command == "pause")
            layer->playing = false;
        else if (command == "stop")
            layer->playing = false, layer->frame = 0;
        else
            return false;
        return true;
    }
    int animationLayerCount(uint32_t layer_id) override {
        int count = 0;
        for (const FakeAnimationLayer& layer : animation_layers_) count += layer.layer_id == layer_id;
        return count;
    }

    bool layerExists(uint32_t id) override {
        return find(id) != nullptr;
    }
    std::string layerName(uint32_t id) override {
        FakeLayer* layer = find(id);
        return layer ? layer->name : "";
    }
    bool getVector(uint32_t id, const std::string& property, double out[3], int& components) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        const double* source = property == "origin"   ? layer->origin
                               : property == "scale"  ? layer->scale
                               : property == "angles" ? layer->angles
                                                      : nullptr;
        if (source) {
            for (int i = 0; i < 3; ++i) out[i] = source[i];
            components = 3;
            return true;
        }
        if (property == "size") {
            out[0] = layer->size[0];
            out[1] = layer->size[1];
            components = 2;
            return true;
        }
        return false;
    }
    bool setVector(uint32_t id, const std::string& property, const double value[3]) override {
        FakeLayer* layer = find(id);
        double* target = !layer                 ? nullptr
                         : property == "origin" ? layer->origin
                         : property == "scale"  ? layer->scale
                         : property == "angles" ? layer->angles
                                                : nullptr;
        if (!target) return false;
        for (int i = 0; i < 3; ++i) target[i] = value[i];
        return true;
    }
    void loadKinds(const wallpaper_engine::SceneDocument& document) {
        for (const auto& object : document.objects) {
            FakeLayer* layer = find(object.node.id);
            if (!layer) continue;
            if (object.kind == wallpaper_engine::SceneObjectKind::Text) {
                layer->text = true;
                layer->strings = {
                    {"text", ""}, {"font", ""}, {"horizontalalign", "center"}, {"verticalalign", "center"}};
                layer->numbers.insert({{"pointsize", 12}, {"maxwidth", 0}, {"maxrows", 0}});
            } else if (object.kind == wallpaper_engine::SceneObjectKind::Sound) {
                layer->sound = true;
                layer->numbers["volume"] = object.sound.volume;
            }
        }
    }
    bool getWorldMatrix(uint32_t id, double out[16]) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        for (int i = 0; i < 16; ++i) out[i] = i % 5 == 0 ? 1.0 : 0.0;
        for (int i = 0; i < 3; ++i) out[12 + i] = layer->origin[i];
        return true;
    }
    bool layerCommand(uint32_t id, const std::string& command) override {
        FakeLayer* layer = find(id);
        if (!layer || !layer->sound) return false;
        layer->sound_playing = command == "play";
        return true;
    }
    bool getNumber(uint32_t id, const std::string& property, double& out) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        const auto it = layer->numbers.find(property);
        if (it == layer->numbers.end()) return false;
        out = it->second;
        return true;
    }
    bool setNumber(uint32_t id, const std::string& property, double value) override {
        FakeLayer* layer = find(id);
        if (!layer || !layer->numbers.count(property)) return false;
        layer->numbers[property] = value;
        return true;
    }
    bool getString(uint32_t id, const std::string& property, std::string& out) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        const auto it = layer->strings.find(property);
        if (it == layer->strings.end()) return false;
        out = it->second;
        return true;
    }
    bool setString(uint32_t id, const std::string& property, const std::string& value) override {
        FakeLayer* layer = find(id);
        if (!layer || !layer->strings.count(property)) return false;
        layer->strings[property] = value;
        return true;
    }
    bool getBool(uint32_t id, const std::string& property, bool& out) override {
        FakeLayer* layer = find(id);
        if (!layer) return false;
        if (property == "visible") {
            out = layer->visible;
            return true;
        }
        if (property == "playing" && layer->sound) {
            out = layer->sound_playing;
            return true;
        }
        return false;
    }
    bool setBool(uint32_t id, const std::string& property, bool value) override {
        FakeLayer* layer = find(id);
        if (!layer || property != "visible") return false;
        layer->visible = value;
        return true;
    }
    uint32_t parentOf(uint32_t id) override {
        FakeLayer* layer = find(id);
        return layer ? layer->parent : 0;
    }
    std::vector<uint32_t> childrenOf(uint32_t id) override {
        std::vector<uint32_t> children;
        for (const FakeLayer& layer : layers_)
            if (layer.parent == id) children.push_back(layer.id);
        return children;
    }
    uint32_t findLayerByName(const std::string& name) override {
        for (const FakeLayer& layer : layers_)
            if (layer.name == name) return layer.id;
        return 0;
    }
    std::vector<uint32_t> allLayers() override {
        std::vector<uint32_t> ids;
        for (const FakeLayer& layer : layers_) ids.push_back(layer.id);
        return ids;
    }

   private:
    std::vector<FakeLayer> layers_;
    AnimationTimelines timelines_;
    std::vector<FakeAnimationLayer> animation_layers_;
};

// Maps /objects/<index>/... to the id of that object.
uint32_t ownerOf(const cJSON* scene, const std::string& path) {
    const std::string prefix = "/objects/";
    if (path.compare(0, prefix.size(), prefix) != 0) return 0;
    const int index = atoi(path.c_str() + prefix.size());
    const cJSON* object = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(scene, "objects"), index);
    const cJSON* id = object ? cJSON_GetObjectItemCaseSensitive(object, "id") : nullptr;
    return cJSON_IsNumber(id) ? (uint32_t)id->valuedouble : 0;
}

struct ScriptResult {
    std::string path;
    std::string property;
    bool loaded = false;
    bool exercised = false;
    std::string error;
    std::string at;  // the source line the error points at
    double max_update_us = 0.0;
};

// The value a property script receives, typed like the engine does it.
ScriptValue startValue(const ScriptBlock& block, FakeScene& scene) {
    FakeLayer* layer = scene.find(block.object_id);
    ScriptValue value;
    const std::string& property = block.property;
    if (property == "text") {
        value = ScriptValue::makeString("");
    } else if (property == "visible") {
        value = ScriptValue::makeBool(layer ? layer->visible : true);
    } else if (property == "origin" || property == "scale" || property == "angles") {
        double v[3] = {0, 0, 0};
        int components = 0;
        if (layer) scene.getVector(layer->id, property, v, components);
        value = ScriptValue::makeVec3(v[0], v[1], v[2]);
    } else if (property == "color") {
        value = ScriptValue::makeVec3(1, 1, 1);
    } else if (property == "size") {
        value.kind = ScriptValue::Kind::Vec2;
        if (layer) {
            value.vec[0] = layer->size[0];
            value.vec[1] = layer->size[1];
        }
    } else {
        value = ScriptValue::makeNumber(1.0);
    }
    return value;
}

void collect(const cJSON* node, const std::string& path, const std::string& property, std::vector<ScriptBlock>& out) {
    if (cJSON_IsObject(node)) {
        const cJSON* script = cJSON_GetObjectItemCaseSensitive(node, "script");
        if (cJSON_IsString(script) && script->valuestring && script->valuestring[0]) {
            ScriptBlock block;
            block.path = path;
            block.property = property;
            block.source = script->valuestring;
            const cJSON* props = cJSON_GetObjectItemCaseSensitive(node, "scriptproperties");
            if (cJSON_IsObject(props)) {
                if (char* printed = cJSON_PrintUnformatted(props)) {
                    block.properties_json = printed;
                    cJSON_free(printed);
                }
            }
            out.push_back(std::move(block));
        }
        for (const cJSON* child = node->child; child; child = child->next) {
            if (!child->string || strcmp(child->string, "script") == 0 ||
                strcmp(child->string, "scriptproperties") == 0)
                continue;
            collect(child, path + "/" + child->string, child->string, out);
        }
    } else if (cJSON_IsArray(node)) {
        int index = 0;
        for (const cJSON* child = node->child; child; child = child->next, ++index)
            collect(child, path + "/" + std::to_string(index), property, out);
    }
}

std::string readTitle(const std::string& dir) {
    char* text = read_file_to_string((dir + "/project.json").c_str());
    if (!text) return "";
    std::string title;
    if (cJSON* root = cJSON_Parse(text)) {
        const cJSON* node = cJSON_GetObjectItemCaseSensitive(root, "title");
        if (cJSON_IsString(node) && node->valuestring) title = node->valuestring;
        cJSON_Delete(root);
    }
    free(text);
    return title;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
    }
}

// Collapses quoted identifiers so "ReferenceError: 'thisLayer' is not defined" groups by API, not by wallpaper.
std::string normalise(const std::string& message) {
    return message.size() > 160 ? message.substr(0, 160) : message;
}

// The source line a stack trace ("... (script://<id>:<line>)") points at, trimmed for the report.
std::string failingLine(const std::string& source, const std::string& stack) {
    const size_t marker = stack.find("script://");
    if (marker == std::string::npos) return "";
    const size_t colon = stack.find(':', marker + 9);
    if (colon == std::string::npos) return "";
    int line = atoi(stack.c_str() + colon + 1);
    size_t start = 0;
    for (int current = 1; current < line && start != std::string::npos; ++current) {
        start = source.find('\n', start);
        if (start != std::string::npos) ++start;
    }
    if (start == std::string::npos || line < 1) return "";
    const size_t end = source.find('\n', start);
    std::string text = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const size_t first = text.find_first_not_of(" \t");
    text = first == std::string::npos ? "" : text.substr(first);
    return "line " + std::to_string(line) + ": " + text.substr(0, 140);
}

// The engine loads every script of the scene first and only then calls init(), so scripts can use what other
// scripts define at load time (the `shared` helpers); the runner does the same in two phases.
struct LoadedScript {
    const ScriptBlock* block = nullptr;
    std::unique_ptr<SceneScript> script;
    ScriptResult result;
};

LoadedScript loadScript(const ScriptBlock& block) {
    LoadedScript loaded;
    loaded.block = &block;
    loaded.result.path = block.path;
    loaded.result.property = block.property;
    loaded.script = std::make_unique<SceneScript>();
    loaded.script->setLayerId(block.object_id);
    loaded.script->setProperty(block.property);
    loaded.result.loaded = loaded.script->load(block.source, block.properties_json);
    if (!loaded.result.loaded)
        loaded.result.error = loaded.script->lastError().empty() ? "load: failed" : loaded.script->lastError();
    else if (loaded.script->errorCount() > 0)
        loaded.result.error = loaded.script->lastError();
    return loaded;
}

ScriptResult exercise(LoadedScript& loaded, FakeScene& scene, int frames) {
    ScriptResult result = loaded.result;
    SceneScript& script = *loaded.script;
    const ScriptBlock& block = *loaded.block;
    if (!result.error.empty()) return result;

    ScriptValue value = startValue(block, scene);
    if (script.hasFunction("init")) script.initValue(value);

    if (script.valid()) {
        result.exercised = true;
        for (int frame = 0; frame < frames && script.errorCount() == 0; ++frame) {
            ScriptEngine::instance().beginFrame(1.0 / 60.0, frame / 60.0, 3840.0f, 2160.0f, 1920.0f, 1080.0f);
            scene.advance(1.0f / 60.0f);
            const auto start = std::chrono::steady_clock::now();
            script.updateValue(value);
            const double us =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            result.max_update_us = std::max(result.max_update_us, us);
        }
    }
    // Every event hook once, with the shape the engine sends.
    if (script.errorCount() == 0) {
        const ScriptEvent cursor = {{"worldPosition", ScriptValue::makeVec3(960, 540, 0)},
                                    {"localPosition", ScriptValue::makeVec3(10, 10, 0)}};
        for (const char* hook : {"cursorEnter", "cursorMove", "cursorDown", "cursorUp", "cursorClick", "cursorLeave"})
            script.callHook(hook, cursor);
        script.callHook("mediaStatusChanged", {{"enabled", ScriptValue::makeBool(true)}});
        script.callHook("mediaPlaybackChanged", {{"state", ScriptValue::makeNumber(1)}});
        script.callHook("mediaPropertiesChanged", {{"title", ScriptValue::makeString("Title")},
                                                   {"artist", ScriptValue::makeString("Artist")},
                                                   {"albumTitle", ScriptValue::makeString("Album")},
                                                   {"albumArtist", ScriptValue::makeString("Album Artist")},
                                                   {"subTitle", ScriptValue::makeString("")},
                                                   {"genres", ScriptValue::makeString("Genre")},
                                                   {"contentType", ScriptValue::makeString("music")}});
        script.callHook("mediaThumbnailChanged", {{"hasThumbnail", ScriptValue::makeBool(true)},
                                                  {"primaryColor", ScriptValue::makeVec3(0.8, 0.2, 0.2)},
                                                  {"secondaryColor", ScriptValue::makeVec3(0.2, 0.8, 0.2)},
                                                  {"tertiaryColor", ScriptValue::makeVec3(0.2, 0.2, 0.8)},
                                                  {"textColor", ScriptValue::makeVec3(1, 1, 1)},
                                                  {"highContrastColor", ScriptValue::makeVec3(0, 0, 0)}});
        script.callHook("mediaTimelineChanged",
                        {{"position", ScriptValue::makeNumber(10)}, {"duration", ScriptValue::makeNumber(200)}});
        script.callHook("applyUserProperties", {});
        script.callHook("applyGeneralSettings", {{"language", ScriptValue::makeString("en")}});
        script.callHook("resizeScreen", {{"x", ScriptValue::makeNumber(1920)}, {"y", ScriptValue::makeNumber(1080)}});
        script.callHook("destroy", {}, false);
    }
    if (script.errorCount() > 0) {
        result.error = script.lastError();
        result.at = failingLine(block.source, script.lastStack());
        std::string frames = script.lastStack();  // the innermost frames, which can belong to another script
        for (char& c : frames)
            if (c == '\n') c = ' ';
        result.at += " || " + frames.substr(0, 220);
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    std::string root, assets_dir, out_path = "utils/out/script_corpus_report.json",
                                  work = "utils/out/script_corpus_work";
    std::set<std::string> only;
    int frames = 120;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--assets-dir" && i + 1 < argc) {
            assets_dir = argv[++i];
        } else if (arg == "--work" && i + 1 < argc) {
            work = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (arg == "--ids" && i + 1 < argc) {
            std::string list = argv[++i];
            for (size_t start = 0; start <= list.size();) {
                size_t comma = list.find(',', start);
                if (comma == std::string::npos) comma = list.size();
                if (comma > start) only.insert(list.substr(start, comma - start));
                start = comma + 1;
            }
        } else if (root.empty()) {
            root = arg;
        }
    }
    if (root.empty()) {
        fprintf(stderr,
                "usage: %s <workshop-content-dir> [--out report.json] [--work dir] [--ids a,b] [--frames N] "
                "[--assets-dir dir]\n",
                argv[0]);
        return 2;
    }

    // steamapps/workshop/content/431960 -> steamapps/common/wallpaper_engine/assets
    if (assets_dir.empty()) assets_dir = root + "/../../../common/wallpaper_engine/assets";
    ScriptEngine::instance().setAssetsDir(assets_dir);

    std::vector<std::string> ids;
    if (DIR* dir = opendir(root.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            if (!only.empty() && !only.count(entry->d_name)) continue;
            if (fileExists(root + "/" + entry->d_name + "/scene.pkg")) ids.push_back(entry->d_name);
        }
        closedir(dir);
    }
    std::sort(ids.begin(), ids.end());

    cJSON* report = cJSON_CreateObject();
    cJSON* wallpapers = cJSON_AddArrayToObject(report, "wallpapers");
    std::map<std::string, std::set<std::string>> error_wallpapers;
    std::map<std::string, int> error_blocks;
    int total_scripts = 0, loaded = 0, clean = 0, with_scripts = 0, clean_wallpapers = 0;

    for (const std::string& id : ids) {
        const std::string out_dir = work + "/" + id;
        makeDirs(out_dir);
        if (!extract_pkg((root + "/" + id + "/scene.pkg").c_str(), out_dir.c_str())) continue;
        char* text = read_file_to_string((out_dir + "/scene.json").c_str());
        if (!text) continue;
        cJSON* scene = cJSON_Parse(text);
        free(text);
        if (!scene) continue;

        std::vector<ScriptBlock> blocks;
        collect(scene, "", "", blocks);
        for (ScriptBlock& block : blocks) block.object_id = ownerOf(scene, block.path);
        FakeScene fake_scene;
        fake_scene.load(scene);
        wallpaper_engine::SceneDocument document;
        if (parseSceneFile((out_dir + "/scene.json").c_str(), document)) {
            fake_scene.loadKinds(document);
            fake_scene.loadAnimations(document);
            fake_scene.loadAnimationLayers(document);
        }
        cJSON_Delete(scene);
        if (blocks.empty()) continue;
        ScriptEngine::instance().setSceneBackend(&fake_scene);

        ++with_scripts;
        int wallpaper_clean = 0;
        cJSON* entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "id", id.c_str());
        cJSON_AddStringToObject(entry, "title", readTitle(root + "/" + id).c_str());
        cJSON* failures = cJSON_AddArrayToObject(entry, "failures");
        std::vector<LoadedScript> loaded_scripts;
        loaded_scripts.reserve(blocks.size());
        for (const ScriptBlock& block : blocks) loaded_scripts.push_back(loadScript(block));
        for (LoadedScript& loaded_script : loaded_scripts) {
            const ScriptResult result = exercise(loaded_script, fake_scene, frames);
            ++total_scripts;
            if (result.loaded) ++loaded;
            if (result.error.empty()) {
                ++clean;
                ++wallpaper_clean;
                continue;
            }
            const std::string key = normalise(result.error);
            error_wallpapers[key].insert(id);
            ++error_blocks[key];
            cJSON* failure = cJSON_CreateObject();
            cJSON_AddStringToObject(failure, "path", result.path.c_str());
            cJSON_AddStringToObject(failure, "error", key.c_str());
            if (!result.at.empty()) cJSON_AddStringToObject(failure, "at", result.at.c_str());
            cJSON_AddItemToArray(failures, failure);
        }
        cJSON_AddNumberToObject(entry, "scripts", (double)blocks.size());
        cJSON_AddNumberToObject(entry, "clean", wallpaper_clean);
        if (wallpaper_clean == (int)blocks.size()) ++clean_wallpapers;
        cJSON_AddItemToArray(wallpapers, entry);
        ScriptEngine::instance().setSceneBackend(nullptr);  // fake_scene goes out of scope
        fprintf(stderr, "%s: %d/%zu scripts clean\n", id.c_str(), wallpaper_clean, blocks.size());
    }

    cJSON* summary = cJSON_AddObjectToObject(report, "summary");
    cJSON_AddNumberToObject(summary, "wallpapers_scanned", (double)ids.size());
    cJSON_AddNumberToObject(summary, "wallpapers_with_scripts", with_scripts);
    cJSON_AddNumberToObject(summary, "wallpapers_fully_clean", clean_wallpapers);
    cJSON_AddNumberToObject(summary, "script_blocks", total_scripts);
    cJSON_AddNumberToObject(summary, "blocks_loaded", loaded);
    cJSON_AddNumberToObject(summary, "blocks_clean", clean);

    std::vector<std::pair<std::string, int>> ranked(error_blocks.begin(), error_blocks.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    cJSON* errors = cJSON_AddArrayToObject(report, "errors");
    for (const auto& [message, blocks] : ranked) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "error", message.c_str());
        cJSON_AddNumberToObject(item, "blocks", blocks);
        cJSON_AddNumberToObject(item, "wallpapers", (double)error_wallpapers[message].size());
        cJSON_AddItemToArray(errors, item);
    }

    char* printed = cJSON_Print(report);
    const size_t slash = out_path.rfind('/');
    if (slash != std::string::npos) makeDirs(out_path.substr(0, slash));
    if (FILE* file = fopen(out_path.c_str(), "w")) {
        fputs(printed, file);
        fclose(file);
    }
    cJSON_free(printed);
    cJSON_Delete(report);

    printf("wallpapers with scripts: %d (fully clean %d) | blocks: %d, loaded %d, clean %d\n", with_scripts,
           clean_wallpapers, total_scripts, loaded, clean);
    printf("top errors (blocks / wallpapers):\n");
    for (size_t i = 0; i < ranked.size() && i < 25; ++i)
        printf("%6d %4zu  %s\n", ranked[i].second, error_wallpapers[ranked[i].first].size(), ranked[i].first.c_str());
    printf("report: %s\n", out_path.c_str());
    return 0;
}
