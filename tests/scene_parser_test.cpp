#include "wallpaper/2d/parser/scene_parser.h"

#include <unistd.h>

#include <cstdio>
#include <string>

#include "test_util.h"
#include "wallpaper/user_properties.h"

using namespace wallpaper_engine;

namespace {
const char* kScene = R"JSON({
  "camera": {"center": "0 0 0", "eye": "0 0 1", "up": "0 1 0"},
  "general": {"clearcolor": "0.1 0.2 0.3", "bloom": true},
  "objects": [
    {"id": 1, "name": "Background", "image": "models/bg.json", "size": "1920 1080",
     "color": "0.5 0.25 1", "visible": true,
     "alpha": {"value": 0.8, "animation": {"c0": [{"frame": 0, "value": 0}, {"frame": 30, "value": 1}],
               "options": {"fps": 24, "length": 60, "mode": "mirror"}}},
     "effects": [{"file": "effects/blur/effect.json", "visible": false}, {"file": 7}]},
    {"id": 2, "name": "Rain", "particle": "particles/rain.json",
     "instanceoverride": {"alpha": 0.19, "count": 0.05, "rate": 1.74, "size": 5, "speed": 2.63, "colorn": "1 0 0"}},
    {"id": 3, "name": "Clock", "text": {"value": "00:00", "script": "export function update() {}",
     "scriptproperties": {"use24h": true}}, "font": "fonts/a.ttf", "pointsize": 40,
     "horizontalalign": "left", "maxrows": 3},
    {"id": 4, "name": "Music", "sound": ["a.mp3", "b.mp3"], "playbackmode": "random", "volume": 0.5,
     "startsilent": true, "mintime": 2, "maxtime": 9},
    {"id": 5, "name": "Single", "sound": "c.ogg", "playbackmode": "loop"},
    {"id": 6, "name": "Nothing"}
  ]
})JSON";

const char* kScriptedScene = R"JSON({
  "camera": {"center": "0 0 0", "eye": "0 0 1", "up": "0 1 0"},
  "objects": [
    {"id": 1, "name": "Scripted", "image": "models/a.json",
     "origin": {"script": "export function update(v) {}", "scriptproperties": {"k": {"user": "x", "value": 1}}, "value": "1 2 3"},
     "scale": {"script": "export function update(v) {}", "scriptproperties": {"k": {"user": "x", "value": 1}}, "value": "4 5 6"},
     "angles": {"script": "export function update(v) {}", "scriptproperties": {"k": {"user": "x", "value": 1}}, "value": "7 8 9"},
     "visible": {"script": "export function update(v) {}", "scriptproperties": {"k": {"user": "x", "value": 1}}, "value": false},
     "color": {"script": "export function update(v) {}", "scriptproperties": {"k": {"user": "x", "value": 1}}, "value": "0.1 0.2 0.3"},
     "size": {"script": "export function update(v) {}", "scriptproperties": {"k": {"user": "x", "value": 1}}, "value": "10 20"}},
    {"id": 2, "name": "Plain", "image": "models/b.json",
     "origin": "1 1 1", "scale": "2 2 2", "angles": "0 0 0", "visible": true,
     "color": "1 1 1", "size": "5 5"}
  ]
})JSON";

const char* kEffectScene = R"JSON({
  "camera": {"center": "0 0 0", "eye": "0 0 1", "up": "0 1 0"},
  "objects": [
    {"id": 1, "name": "Fx", "image": "models/a.json",
     "effects": [
       {"file": "effects/glow/effect.json",
        "visible": {"script": "export function update(v) {}", "scriptproperties": {"k": 1}, "value": false},
        "passes": [
          {"constantshadervalues": {"strength": {"script": "export function update(v) {}",
                                                 "scriptproperties": {"k": 1}, "value": 0.5},
                                    "count": 4,
                                    "pulse": {"animation": {"c0": [{"frame": 0, "value": 0}]}}}},
          {"constantshadervalues": {"tint": "1 0 0"}}
        ]},
       {"file": "effects/plain/effect.json", "visible": true}
     ]}
  ]
})JSON";

const char* kAnimatedScene = R"JSON({
  "camera": {"center": "0 0 0", "eye": "0 0 1", "up": "0 1 0"},
  "objects": [
    {"id": 1, "name": "Animated", "image": "models/a.json", "origin": "5 6 7",
     "scale": {"value": "1 1 1", "animation": {
        "relative": true,
        "c0": [{"frame": 0, "value": 1}, {"frame": 30, "value": 2}, {"frame": 60, "value": 3}],
        "c1": [{"frame": 0, "value": 4}, {"frame": 30, "value": 5}, {"frame": 60, "value": 6}],
        "c2": [{"frame": 0, "value": 7}, {"frame": 30, "value": 8}, {"frame": 60, "value": 9}],
        "options": {"fps": 60, "length": 100, "mode": "loop", "name": "Pulse",
                    "children": [{"key": "alpha"}]}}},
     "alpha": {"value": 0.5, "animation": {
        "c0": [{"frame": 0, "value": 0}, {"frame": 15, "value": 1}],
        "options": {"fps": 60, "length": 100, "mode": "loop", "startpaused": true,
                    "parent": {"key": "scale"}}}}}
  ]
})JSON";

SceneDocument parseText(const char* json) {
    char path[] = "/tmp/lwe_scene_XXXXXX";
    const int fd = mkstemp(path);
    SceneDocument doc;
    if (fd < 0) return doc;
    write(fd, json, std::char_traits<char>::length(json));
    close(fd);
    test::expect("scene", parseSceneFile(path, doc), "scene file parses");
    unlink(path);
    return doc;
}

void testObjects() {
    const SceneDocument doc = parseText(kScene);
    test::expect("objects", doc.objects.size() == 6, "every object is kept");
    if (doc.objects.size() != 6) return;

    const SceneObjectDocument& image = doc.objects[0];
    test::expect("image", image.kind == SceneObjectKind::Image, "image kind");
    test::expect("image", image.name == "Background" && image.image.image == "models/bg.json", "name and image");
    test::expect("image", image.image.size[0] == 1920.0f && image.image.color[1] == 0.25f, "size and color");
    test::expect("image", image.image.alpha == 0.8f, "animated alpha keeps its fallback value");
    test::expect("image", image.image.alpha_keys.size() == 2 && image.image.alpha_keys[1].frame == 30.0f,
                 "alpha keyframes");
    test::expect("image", image.image.alpha_fps == 24.0f && image.image.alpha_mode == "mirror", "alpha options");
    test::expect("image", image.effects.size() == 1 && !image.effects[0].visible, "effect without a file is skipped");

    const ParticleObjectDocument& particle = doc.objects[1].particle;
    test::expect("particle", doc.objects[1].kind == SceneObjectKind::Particle, "particle kind");
    test::expect("particle", particle.particle == "particles/rain.json", "particle path");
    test::expect("particle", particle.override_count == 0.05f && particle.override_speed == 2.63f,
                 "count and speed overrides");
    test::expect("particle", particle.override_rate == 1.74f && particle.override_size == 5.0f, "rate and size");
    test::expect("particle", particle.has_override_color && !particle.override_color_is_legacy, "colorn is linear");

    const TextObjectDocument& text = doc.objects[2].text;
    test::expect("text", doc.objects[2].kind == SceneObjectKind::Text, "text kind");
    test::expect("text", text.text == "00:00" && text.script.find("update") != std::string::npos, "text and script");
    test::expect("text", text.script_properties_json.find("use24h") != std::string::npos, "script properties");
    test::expect("text", text.font == "fonts/a.ttf" && text.pointsize == 40.0f, "font");
    test::expect("text", text.horizontal_align == "left" && text.vertical_align == "center" && text.max_rows == 3,
                 "alignment and rows");

    const SoundObjectDocument& music = doc.objects[3].sound;
    test::expect("sound", doc.objects[3].kind == SceneObjectKind::Sound, "sound kind");
    test::expect("sound", music.sounds.size() == 2 && music.playback_mode == SoundPlaybackMode::Random,
                 "sound list and mode");
    test::expect("sound",
                 music.volume == 0.5f && music.start_silent && music.min_time == 2.0f && music.max_time == 9.0f,
                 "sound timing");
    const SoundObjectDocument& single = doc.objects[4].sound;
    test::expect("sound", single.sounds.size() == 1 && single.sounds[0] == "c.ogg", "a lone sound string");
    test::expect("sound", single.playback_mode == SoundPlaybackMode::Loop, "loop mode");

    test::expect("unknown", doc.objects[5].kind == SceneObjectKind::Unknown, "an object with no payload is unknown");
}

void testScriptedValues() {
    const SceneDocument doc = parseText(kScriptedScene);
    test::expect("scripted", doc.objects.size() == 2, "both objects are kept");
    if (doc.objects.size() != 2) return;

    const SceneObjectDocument& scripted = doc.objects[0];
    test::expect("scripted", scripted.node.origin[2] == 3.0f, "scripted origin keeps its value");
    test::expect("scripted", scripted.node.scale[0] == 4.0f, "scripted scale keeps its value");
    test::expect("scripted", scripted.node.angles[1] == 8.0f, "scripted angles keep their value");
    test::expect("scripted", !scripted.visible, "scripted visible keeps its value");
    test::expect("scripted", scripted.image.color[1] == 0.2f, "scripted color keeps its value");
    test::expect("scripted", scripted.image.size[0] == 10.0f, "scripted size keeps its value");

    test::expect(
        "scripted",
        !scripted.node.origin_script.empty() && scripted.node.origin_script.script.find("update") != std::string::npos,
        "origin script captured");
    test::expect("scripted", scripted.node.origin_script.properties_json.find("k") != std::string::npos,
                 "origin script properties captured");
    test::expect("scripted", !scripted.node.scale_script.empty(), "scale script captured");
    test::expect("scripted", !scripted.node.angles_script.empty(), "angles script captured");
    test::expect("scripted", !scripted.visible_script.empty(), "visible script captured");
    test::expect("scripted", !scripted.image.color_script.empty(), "color script captured");
    test::expect("scripted", !scripted.image.size_script.empty(), "size script captured");

    const SceneObjectDocument& plain = doc.objects[1];
    test::expect("scripted",
                 plain.node.origin_script.empty() && plain.node.scale_script.empty() &&
                     plain.node.angles_script.empty() && plain.visible_script.empty() &&
                     plain.image.color_script.empty() && plain.image.size_script.empty(),
                 "plain values have no scripts");
}

void testEffectScripts() {
    const SceneDocument doc = parseText(kEffectScene);
    test::expect("effect", doc.objects.size() == 1, "effect object is kept");
    if (doc.objects.size() != 1) return;
    test::expect("effect", doc.objects[0].effects.size() == 2, "both effects are kept");
    if (doc.objects[0].effects.size() != 2) return;

    const EffectInstanceDocument& scripted = doc.objects[0].effects[0];
    test::expect("effect", !scripted.visible, "scripted visible keeps its base value");
    test::expect("effect",
                 scripted.visible_script.script.find("update") != std::string::npos &&
                     scripted.visible_script.properties_json.find("k") != std::string::npos,
                 "visible script and properties captured");
    test::expect("effect", scripted.constant_scripts.size() == 1, "only the scripted constant is kept");
    if (scripted.constant_scripts.size() == 1) {
        const EffectConstantScript& constant = scripted.constant_scripts[0];
        test::expect("effect", constant.pass == 0 && constant.name == "strength", "constant pass and name");
        test::expect("effect", !constant.script.empty(), "constant script captured");
    }

    const EffectInstanceDocument& plain = doc.objects[0].effects[1];
    test::expect("effect", plain.visible && plain.visible_script.empty(), "plain visible has no script");
    test::expect("effect", plain.constant_scripts.empty(), "plain effect has no constant scripts");
}

const PropertyAnimationDocument* findAnimation(const SceneObjectDocument& object, const std::string& property) {
    for (const PropertyAnimationDocument& animation : object.animations) {
        if (animation.property == property) return &animation;
    }
    return nullptr;
}

void testPropertyAnimations() {
    const SceneDocument doc = parseText(kAnimatedScene);
    test::expect("animation", doc.objects.size() == 1, "the animated object is kept");
    if (doc.objects.size() != 1) return;

    const SceneObjectDocument& object = doc.objects[0];
    test::expect("animation", object.animations.size() == 2, "a plain origin stays out, scale and alpha animate");
    const PropertyAnimationDocument* scale = findAnimation(object, "scale");
    const PropertyAnimationDocument* alpha = findAnimation(object, "alpha");
    test::expect("animation", scale && alpha, "both animated properties are reported");
    if (!scale || !alpha) return;

    test::expect(
        "animation",
        scale->curves[0].keys.size() == 3 && scale->curves[1].keys.size() == 3 && scale->curves[2].keys.size() == 3,
        "every scale channel has keyframes");
    test::expect("animation",
                 scale->curves[0].keys[2].frame == 60.0f && scale->curves[0].keys[2].value == 3.0f &&
                     scale->curves[1].keys[0].value == 4.0f && scale->curves[2].keys[1].frame == 30.0f &&
                     scale->curves[2].keys[1].value == 8.0f,
                 "scale frame and value pairs");
    test::expect("animation",
                 scale->curves[0].fps == 60.0f && scale->curves[0].length == 100.0f && scale->curves[1].fps == 60.0f &&
                     scale->curves[1].mode == "loop" && scale->curves[2].fps == 60.0f &&
                     scale->curves[2].length == 100.0f && scale->curves[2].mode == "loop",
                 "options reach every channel");
    test::expect("animation", scale->name == "Pulse" && scale->relative, "name and relative");
    test::expect("animation", scale->children.size() == 1 && scale->children[0] == "alpha", "children listing");
    test::expect("animation", scale->parent.empty(), "the timeline root has no parent");

    test::expect("animation", alpha->curves[0].keys.size() == 2 && alpha->curves[1].keys.empty(), "alpha uses c0 only");
    test::expect("animation", alpha->curves[0].keys[1].frame == 15.0f && alpha->curves[0].keys[1].value == 1.0f,
                 "alpha frame and value pairs");
    test::expect("animation",
                 alpha->curves[0].fps == 60.0f && alpha->curves[0].length == 100.0f && alpha->curves[0].mode == "loop",
                 "alpha options");
    test::expect("animation", alpha->parent == "scale" && alpha->start_paused && !alpha->relative,
                 "alpha links to the scale timeline and starts paused");

    test::expect("animation", object.image.alpha_keys.size() == 2 && object.image.alpha_keys[1].frame == 15.0f,
                 "the legacy alpha keys still parse");
}

std::string writeTemp(const char* text) {
    char path[] = "/tmp/lwe_user_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) return "";
    write(fd, text, std::char_traits<char>::length(text));
    close(fd);
    return path;
}

void testUserBindings() {
    const char* project = R"JSON({"general": {"properties": {
      "opacity": {"type": "slider", "value": 1.0}, "showFish": {"type": "bool", "value": true},
      "tint": {"type": "color", "value": "1 1 1"}, "mode": {"type": "combo", "value": 1},
      "speed": {"type": "slider", "value": 2}}}})JSON";
    const char* scene = R"JSON({
      "camera": {"center": "0 0 0", "eye": "0 0 1", "up": "0 1 0"},
      "objects": [
        {"id": 1, "name": "A", "image": "models/a.json", "alpha": {"user": "opacity", "value": 0.5},
         "visible": {"user": "showFish", "value": true}, "color": {"user": "tint", "value": "1 1 1"},
         "origin": {"script": "export function update(v) { return v; }", "value": "0 0 0",
                    "scriptproperties": {"speed": {"user": "speed", "value": 1}}}},
        {"id": 2, "name": "B", "image": "models/b.json",
         "visible": {"user": {"name": "mode", "condition": "2"}, "value": false}},
        {"id": 3, "name": "C", "image": "models/c.json", "visible": {"user": "unknownKey", "value": false}}
      ]})JSON";
    const std::string project_path = writeTemp(project);
    const std::string scene_path = writeTemp(scene);

    UserProperties properties;
    test::expect("user", properties.loadProject(project_path), "project defaults load");
    properties.setFromString("opacity", "0.25");
    properties.setFromString("showFish", "0");
    properties.setFromString("tint", "0 0.5 1");
    properties.setFromString("mode", "2");
    properties.setFromString("speed", "4");

    SceneDocument resolved, defaults;
    test::expect("user", parseSceneFile(scene_path.c_str(), resolved, &properties), "scene parses with properties");
    test::expect("user", parseSceneFile(scene_path.c_str(), defaults), "scene parses without properties");
    unlink(project_path.c_str());
    unlink(scene_path.c_str());
    if (resolved.objects.size() != 3 || defaults.objects.size() != 3) return;

    const SceneObjectDocument& a = resolved.objects[0];
    test::expect("user", a.image.alpha == 0.25f && !a.visible, "plain bindings take the property value");
    test::expect("user", a.image.color[0] == 0.0f && a.image.color[1] == 0.5f && a.image.color[2] == 1.0f,
                 "a color property fills the vector");
    test::expect("user", a.node.origin_script.properties_json.find("\"value\":4") != std::string::npos,
                 "scriptproperties bindings are resolved too");
    test::expect("user", resolved.objects[1].visible, "a condition binding is true while the property equals it");
    test::expect("user", !resolved.objects[2].visible, "an unknown property keeps its default");

    test::expect("user", defaults.objects[0].image.alpha == 0.5f && defaults.objects[0].visible,
                 "without properties the defaults are untouched");
    test::expect("user", !defaults.objects[1].visible, "and so is a condition binding");
}

void testMissingFile() {
    SceneDocument doc;
    test::expect("missing", !parseSceneFile("/tmp/lwe_no_such_scene.json", doc), "a missing file fails");
}
}  // namespace

int main() {
    testObjects();
    testScriptedValues();
    testEffectScripts();
    testPropertyAnimations();
    testUserBindings();
    testMissingFile();
    return test::finish("scene parser tests");
}
