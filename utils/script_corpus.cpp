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
#include <set>
#include <string>
#include <vector>

#include "shared/assets/unpack.h"
#include "shared/core/utils.h"
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
    bool getBool(uint32_t id, const std::string& property, bool& out) override {
        FakeLayer* layer = find(id);
        if (!layer || property != "visible") return false;
        out = layer->visible;
        return true;
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

ScriptResult run(const ScriptBlock& block, FakeScene& scene, int frames) {
    ScriptResult result;
    result.path = block.path;
    result.property = block.property;

    SceneScript script;
    script.setLayerId(block.object_id);
    result.loaded = script.load(block.source, block.properties_json);
    if (!result.loaded) {
        result.error = script.lastError().empty() ? "load: failed" : script.lastError();
        return result;
    }
    if (script.errorCount() > 0) {
        result.error = script.lastError();
        return result;
    }

    ScriptValue value = startValue(block, scene);
    if (script.hasFunction("init")) script.initValue(value);

    if (script.valid()) {
        result.exercised = true;
        for (int frame = 0; frame < frames && script.errorCount() == 0; ++frame) {
            ScriptEngine::instance().beginFrame(1.0 / 60.0, frame / 60.0, 3840.0f, 2160.0f, 1920.0f, 1080.0f);
            const auto start = std::chrono::steady_clock::now();
            script.updateValue(value);
            const double us =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            result.max_update_us = std::max(result.max_update_us, us);
        }
    }
    if (script.errorCount() > 0) result.error = script.lastError();
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
        cJSON_Delete(scene);
        if (blocks.empty()) continue;
        ScriptEngine::instance().setSceneBackend(&fake_scene);

        ++with_scripts;
        int wallpaper_clean = 0;
        cJSON* entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "id", id.c_str());
        cJSON_AddStringToObject(entry, "title", readTitle(root + "/" + id).c_str());
        cJSON* failures = cJSON_AddArrayToObject(entry, "failures");
        for (const ScriptBlock& block : blocks) {
            const ScriptResult result = run(block, fake_scene, frames);
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
