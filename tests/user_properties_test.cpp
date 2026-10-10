// Checks for user property resolution from synthetic project and GUI JSON.

#include "wallpaper/user_properties.h"

#include <stdlib.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_util.h"
using test::expect;

namespace fs = std::filesystem;

namespace {

const char* kProject = R"({
  "type": "scene",
  "general": {
    "properties": {
      "enabled": {"type": "bool", "value": true},
      "speed": {"type": "slider", "value": 0.5, "min": 0, "max": 1},
      "tint": {"type": "color", "value": "0.2 0.4 1"},
      "mode": {"type": "combo", "value": "1", "options": [{"label": "A", "value": "0"}, {"label": "B", "value": "1"}]},
      "modeLabel": {"type": "combo", "value": "fill"},
      "title": {"type": "textinput", "value": "hello"}
    }
  }
})";

const char* kGui = R"({
  "wallpaperProperties": {
    "123": {"enabled": "0", "speed": "0.25", "tint": "255 128 64"},
    "456": {"speed": "0.9"}
  }
})";

bool near(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

UserProperties loadInto(const fs::path& dir) {
    UserProperties properties;
    expect("load", properties.loadProject((dir / "project.json").string()), "project.json should load");
    return properties;
}

void testDefaults(const fs::path& dir) {
    UserProperties props = loadInto(dir);
    expect("defaults", props.all().size() == 6, "every property is collected");

    const UserPropertyValue* enabled = props.find("enabled");
    expect("defaults", enabled && enabled->type == UserPropertyValue::Type::Bool && enabled->b, "bool default");
    const UserPropertyValue* speed = props.find("speed");
    expect("defaults", speed && speed->type == UserPropertyValue::Type::Number && near(speed->n, 0.5),
           "slider default");
    const UserPropertyValue* tint = props.find("tint");
    expect("defaults",
           tint && tint->type == UserPropertyValue::Type::Color && near(tint->color[0], 0.2f) &&
               near(tint->color[1], 0.4f) && near(tint->color[2], 1.0f),
           "color default");
    const UserPropertyValue* mode = props.find("mode");
    expect("defaults", mode && mode->type == UserPropertyValue::Type::Number && near(mode->n, 1.0), "numeric combo");
    const UserPropertyValue* modeLabel = props.find("modeLabel");
    expect("defaults", modeLabel && modeLabel->type == UserPropertyValue::Type::Text && modeLabel->text == "fill",
           "non-numeric combo stays text");
    const UserPropertyValue* title = props.find("title");
    expect("defaults", title && title->type == UserPropertyValue::Type::Text && title->text == "hello", "text default");
    expect("defaults", props.find("missing") == nullptr, "unknown key is absent");
}

void testSavedOverrides(const fs::path& dir) {
    UserProperties props = loadInto(dir);
    props.applySaved(kGui, "123");

    const UserPropertyValue* enabled = props.find("enabled");
    expect("saved", enabled && enabled->type == UserPropertyValue::Type::Bool && !enabled->b, "bool override");
    const UserPropertyValue* speed = props.find("speed");
    expect("saved", speed && speed->type == UserPropertyValue::Type::Number && near(speed->n, 0.25), "slider override");
    const UserPropertyValue* tint = props.find("tint");
    expect("saved",
           tint && tint->type == UserPropertyValue::Type::Color && near(tint->color[0], 1.0f) &&
               near(tint->color[1], 128.0f / 255.0f) && near(tint->color[2], 64.0f / 255.0f),
           "0..255 color normalizes");
    const UserPropertyValue* title = props.find("title");
    expect("saved", title && title->text == "hello", "untouched key keeps its default");

    UserProperties other = loadInto(dir);
    other.applySaved(kGui, "456");
    expect("saved", near(other.find("speed")->n, 0.9), "overrides are scoped to the workshop id");
    expect("saved", other.find("enabled")->b, "other workshop id leaves defaults");
}

void testCliWins(const fs::path& dir) {
    UserProperties props = loadInto(dir);
    props.applySaved(kGui, "123");
    props.setFromString("speed", "0.75");
    props.setFromString("modeLabel", "9");
    expect("cli", near(props.find("speed")->n, 0.75), "cli value wins over saved");
    expect("cli",
           props.find("modeLabel")->type == UserPropertyValue::Type::Number && near(props.find("modeLabel")->n, 9.0),
           "numeric combo value becomes a number");
}

void testPreset(const fs::path& dir) {
    const char* base_json = R"({
  "general": {"properties": {
    "enabled": {"type": "bool", "value": true},
    "tint": {"type": "color", "value": "0 0 0"},
    "photo": {"type": "scenetexture", "value": ""}
  }}
})";
    const fs::path preset = dir / "preset";
    fs::create_directories(preset);
    std::ofstream(preset / "project.json", std::ios::binary) << R"({
  "preset": {"enabled": false, "tint": "255 0 0", "photo": "files/a.jpg", "unknown": "x", "gone": null}
})";
    std::ofstream(dir / "base.json", std::ios::binary) << base_json;

    UserProperties props;
    expect("preset base", props.loadProject((dir / "base.json").string()), "base defaults should load");
    props.applyPreset((preset / "project.json").string(), preset.string());
    expect("preset", !props.find("enabled")->b, "preset overrides a bool default");
    expect("preset", near(props.find("tint")->color[0], 1.0f), "preset color is parsed in 0..255");
    expect("preset", props.find("photo")->text == preset.string() + "/files/a.jpg",
           "relative texture resolves beside the preset");
    expect("preset", props.find("unknown") == nullptr, "undeclared preset keys are ignored");

    props.applySaved(kGui, "123");
    expect("preset", props.find("photo")->text == preset.string() + "/files/a.jpg", "preset value survives saved state");
    props.setFromString("enabled", "1");
    expect("preset", props.find("enabled")->b, "command line still wins over the preset");
}

void testLabelDefaults(const fs::path& dir) {
    fs::path label_json = dir / "label.json";
    std::ofstream(label_json, std::ios::binary) << R"({
  "general": {"properties": {
    "newproperty": {"type": "text", "value": false},
    "caption": {"type": "text", "value": "shown"}
  }}
})";
    UserProperties props;
    expect("label", props.loadProject(label_json.string()), "label project should load");
    expect("label", props.find("newproperty")->text.empty(), "a bool default on a label is not text");
    expect("label", props.find("caption")->text == "shown", "a string label keeps its text");

    fs::path preset = dir / "label_preset";
    fs::create_directories(preset);
    std::ofstream(preset / "project.json", std::ios::binary) << R"({"preset": {"newproperty": false}})";
    props.applyPreset((preset / "project.json").string(), preset.string());
    expect("label", props.find("newproperty")->text.empty(), "a preset bool on a label stays empty");
}

void testTypeParsing(const fs::path& dir) {
    UserProperties props = loadInto(dir);
    props.setFromString("enabled", "1");
    expect("parse", props.find("enabled")->b, "bool accepts 1");
    props.setFromString("enabled", "FALSE");
    expect("parse", !props.find("enabled")->b, "bool accepts false in any case");
    props.setFromString("tint", "0.1 0.2 0.3");
    expect("parse",
           near(props.find("tint")->color[0], 0.1f) && near(props.find("tint")->color[1], 0.2f) &&
               near(props.find("tint")->color[2], 0.3f),
           "0..1 color is kept");
    props.setFromString("tint", "not a color");
    expect("parse", props.find("tint")->type == UserPropertyValue::Type::Text, "unparseable color stays text");
    props.setFromString("mode", "fill");
    expect("parse", props.find("mode")->type == UserPropertyValue::Type::Text, "unparseable combo stays text");
    props.setFromString("custom", "anything");
    const UserPropertyValue* custom = props.find("custom");
    expect("parse", custom && custom->type == UserPropertyValue::Type::Text && custom->text == "anything",
           "unknown key is stored as text");
}

void testToStringsRoundTrip(const fs::path& dir) {
    UserProperties props = loadInto(dir);
    UserProperties copy = loadInto(dir);
    for (const auto& [key, value] : props.toStrings()) copy.setFromString(key, value);
    expect("strings", near(copy.find("speed")->n, 0.5), "slider survives the text round trip");
    expect("strings", near(copy.find("tint")->color[2], 1.0f), "color survives the text round trip");
    expect("strings", copy.find("enabled")->b, "bool survives the text round trip");
    expect("strings", copy.find("title")->text == "hello", "text survives the text round trip");
}

void testBoundKeys() {
    const std::vector<std::string> bound = {"speed", "tint"};
    expect("bound", touchesBoundKey(bound, {{"speed", "0.1"}}), "a bound key needs a rebuild");
    expect("bound", touchesBoundKey(bound, {{"title", "x"}, {"tint", "1 0 0"}}), "any bound key needs a rebuild");
    expect("bound", !touchesBoundKey(bound, {{"title", "x"}}), "unbound keys update in place");
    expect("bound", !touchesBoundKey({}, {{"speed", "0.1"}}), "no bindings means no rebuild");
}

}  // namespace

int main() {
    char pattern[] = "/tmp/lwe_user_props_test_XXXXXX";
    if (!mkdtemp(pattern)) {
        std::printf("FAIL: cannot create temp dir\n");
        return 1;
    }
    const fs::path base = pattern;

    {
        std::ofstream(base / "project.json", std::ios::binary) << kProject;
        testDefaults(base);
        testSavedOverrides(base);
        testCliWins(base);
        testPreset(base);
        testLabelDefaults(base);
        testTypeParsing(base);
        testToStringsRoundTrip(base);
    }
    testBoundKeys();

    UserProperties missing;
    expect("missing", !missing.loadProject((base / "nope.json").string()), "missing project.json fails to load");

    fs::remove_all(base);
    return test::finish("user properties tests");
}
