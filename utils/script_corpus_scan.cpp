#include "script_corpus_scan.h"

#include <cjson/cJSON.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "script_corpus_fake_scene.h"
#include "shared/core/utils.h"
#include "wallpaper/2d/script/script_engine.h"

namespace script_corpus {

uint32_t ownerOf(const cJSON* scene, const std::string& path) {
    const std::string prefix = "/objects/";
    if (path.compare(0, prefix.size(), prefix) != 0) return 0;
    const int index = atoi(path.c_str() + prefix.size());
    const cJSON* object = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(scene, "objects"), index);
    const cJSON* id = object ? cJSON_GetObjectItemCaseSensitive(object, "id") : nullptr;
    return cJSON_IsNumber(id) ? (uint32_t)id->valuedouble : 0;
}

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

std::string normalise(const std::string& message) {
    return message.size() > 160 ? message.substr(0, 160) : message;
}

// Source line from a stack trace (`script://<id>:<line>`), trimmed for the report.
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

}  // namespace script_corpus
