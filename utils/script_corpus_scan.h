#ifndef SCRIPT_CORPUS_SCAN_H
#define SCRIPT_CORPUS_SCAN_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "wallpaper/2d/script/scene_script.h"

struct cJSON;

namespace script_corpus {

class FakeScene;

struct ScriptBlock {
    std::string path;  // JSON path of the owning property, e.g. /objects/12/origin
    std::string property;
    std::string source;
    std::string properties_json;
    uint32_t object_id = 0;  // scene object that owns the property (thisLayer), 0 for scene-level scripts
};

struct ScriptResult {
    std::string path;
    std::string property;
    bool loaded = false;
    bool exercised = false;
    std::string error;
    std::string at;  // the source line the error points at
    double max_update_us = 0.0;
};

struct LoadedScript {
    const ScriptBlock* block = nullptr;
    std::unique_ptr<SceneScript> script;
    ScriptResult result;
};

uint32_t ownerOf(const cJSON* scene, const std::string& path);
ScriptValue startValue(const ScriptBlock& block, FakeScene& scene);
void collect(const cJSON* node, const std::string& path, const std::string& property, std::vector<ScriptBlock>& out);
std::string readTitle(const std::string& dir);
bool fileExists(const std::string& path);
void makeDirs(const std::string& path);
std::string normalise(const std::string& message);
std::string failingLine(const std::string& source, const std::string& stack);
LoadedScript loadScript(const ScriptBlock& block);
ScriptResult exercise(LoadedScript& loaded, FakeScene& scene, int frames);

}  // namespace script_corpus

#endif  // SCRIPT_CORPUS_SCAN_H
