// Checks that every member listed in script_api_manifest.txt exists on the live script globals.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_util.h"
#include "wallpaper/2d/script/scene_script.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/2d/script/script_scene_backend.h"

namespace {

// A scene with one layer (id 3) that has an effect, a particle system and a video, so every handle exists.
class ManifestScene : public ScriptSceneBackend {
   public:
    bool layerExists(uint32_t id) override {
        return id == 3;
    }
    std::string layerName(uint32_t) override {
        return "layer";
    }
    bool getVector(uint32_t, const std::string& property, double out[3], int& components) override {
        if (property != "origin" && property != "scale" && property != "angles" && property != "size" &&
            property != "parallaxDepth")
            return false;
        out[0] = out[1] = out[2] = 1.0;
        components = property == "size" || property == "parallaxDepth" ? 2 : 3;
        return true;
    }
    bool setVector(uint32_t, const std::string&, const double[3]) override {
        return true;
    }
    bool getBool(uint32_t, const std::string& property, bool& out) override {
        if (property != "visible" && property != "video.playing") return false;
        out = true;
        return true;
    }
    bool setBool(uint32_t, const std::string&, bool) override {
        return true;
    }
    bool getNumber(uint32_t, const std::string& property, double& out) override {
        if (property.rfind("particle.", 0) != 0) return false;
        out = 1.0;
        return true;
    }
    uint32_t parentOf(uint32_t) override {
        return 0;
    }
    std::vector<uint32_t> childrenOf(uint32_t) override {
        return {};
    }
    uint32_t findLayerByName(const std::string&) override {
        return 3;
    }
    std::vector<uint32_t> allLayers() override {
        return {3};
    }
    int effectCount(uint32_t) override {
        return 1;
    }
    std::string effectName(uint32_t, int) override {
        return "effect";
    }
    bool effectVisible(uint32_t, int, bool& out) override {
        out = true;
        return true;
    }
    bool getSceneProperty(const std::string&, std::vector<double>& out) override {
        out = {1.0, 1.0, 1.0};
        return true;
    }
};

std::string manifestPath() {
    return (std::filesystem::path(__FILE__).parent_path() / "script_api_manifest.txt").string();
}

}  // namespace

int main() {
    std::vector<std::pair<std::string, std::string>> entries;
    {
        std::ifstream file(manifestPath());
        CHECK(file.good());
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t split = line.rfind(' ');
            entries.emplace_back(line.substr(0, split), line.substr(split + 1));
        }
    }
    CHECK(entries.size() > 50);

    ManifestScene scene;
    ScriptEngine& engine = ScriptEngine::instance();
    const int scope = 0;
    engine.registerScope(&scope, &scene);
    engine.setCreationScope(&scope);

    // The script reports every entry whose expression is missing or has the wrong type.
    std::ostringstream list;
    list << "[";
    for (size_t i = 0; i < entries.size(); ++i) list << (i ? "," : "") << "[\"" << entries[i].first << "\"]";
    list << "]";
    const std::string source =
        "export function update() {\n"
        "  var entries = " + list.str() + ";\n"
        "  var types = {};\n"
        "  var bad = [];\n"
        "  entries.forEach(function (e) {\n"
        "    var t;\n"
        "    try { t = typeof (0, eval)(e[0]); } catch (err) { t = 'error ' + err; }\n"
        "    types[e[0]] = t;\n"
        "  });\n"
        "  return JSON.stringify(types);\n"
        "}\n";

    SceneScript script;
    script.setLayerId(3);
    CHECK(script.load(source, ""));
    ScriptValue result = ScriptValue::makeString("");
    CHECK(script.updateValue(result));

    // Pull each "expression":"type" pair back out of the flat JSON the script produced.
    int missing = 0;
    for (const auto& [expression, expected] : entries) {
        const std::string needle = "\"" + expression + "\":\"" + expected + "\"";
        if (result.text.find(needle) == std::string::npos) {
            ++missing;
            std::printf("manifest: %s is not %s\n", expression.c_str(), expected.c_str());
        }
    }
    CHECK(missing == 0);

    engine.unregisterScope(&scope);
    engine.setCreationScope(nullptr);
    return test::finish("script manifest tests");
}
