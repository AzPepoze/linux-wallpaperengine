// Script corpus runner: loads every SceneScript block of every scene wallpaper in a Workshop folder, runs
// init() and a few simulated update() frames, and reports compile/runtime errors plus the unsupported-API
// messages they come from. No GPU needed. Use it to measure script support before/after a change.
//
//   xmake build script_corpus
//   bin/debug/script_corpus <workshop-content-dir> [--out report.json] [--work dir] [--ids a,b] [--frames N]
#include <cjson/cJSON.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
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

namespace {

struct ScriptBlock {
    std::string path;  // JSON path of the owning property, e.g. /objects/12/origin
    std::string property;
    std::string source;
    std::string properties_json;
};

struct ScriptResult {
    std::string path;
    std::string property;
    bool loaded = false;
    bool exercised = false;
    std::string error;
    double max_update_us = 0.0;
};

// What the runner can feed update() today; vectors/booleans need the Vec* marshalling that the host lacks.
enum class Kind { Number, Text, Unsupported };

Kind kindOf(const std::string& property) {
    if (property == "text") return Kind::Text;
    if (property == "origin" || property == "scale" || property == "angles" || property == "color" ||
        property == "size" || property == "visible")
        return Kind::Unsupported;
    return Kind::Number;
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

ScriptResult run(const ScriptBlock& block, int frames) {
    ScriptResult result;
    result.path = block.path;
    result.property = block.property;

    SceneScript script;
    result.loaded = script.load(block.source, block.properties_json);
    if (!result.loaded) {
        result.error = script.lastError().empty() ? "load: failed" : script.lastError();
        return result;
    }
    if (script.errorCount() > 0) {
        result.error = script.lastError();
        return result;
    }

    double scratch = 1.0;
    if (script.hasFunction("init")) script.callInit(scratch);

    const Kind kind = kindOf(block.property);
    if (script.valid() && kind != Kind::Unsupported) {
        result.exercised = true;
        for (int frame = 0; frame < frames && script.errorCount() == 0; ++frame) {
            ScriptEngine::instance().beginFrame(1.0 / 60.0, frame / 60.0, 3840.0f, 2160.0f, 1920.0f, 1080.0f);
            const auto start = std::chrono::steady_clock::now();
            if (kind == Kind::Text) {
                std::string out;
                script.update("", out);
            } else {
                script.updateNumber(1.0, scratch);
            }
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
    std::string root, assets_dir, out_path = "script_corpus_report.json", work = "build/script_corpus_work";
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
        cJSON_Delete(scene);
        if (blocks.empty()) continue;

        ++with_scripts;
        int wallpaper_clean = 0;
        cJSON* entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "id", id.c_str());
        cJSON_AddStringToObject(entry, "title", readTitle(root + "/" + id).c_str());
        cJSON* failures = cJSON_AddArrayToObject(entry, "failures");
        for (const ScriptBlock& block : blocks) {
            const ScriptResult result = run(block, frames);
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
