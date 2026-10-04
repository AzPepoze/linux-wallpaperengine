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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "script_corpus_fake_scene.h"
#include "script_corpus_scan.h"
#include "shared/assets/unpack.h"
#include "shared/core/utils.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/script/script_engine.h"

using namespace script_corpus;

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
