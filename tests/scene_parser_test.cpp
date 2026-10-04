#include "wallpaper/2d/parser/scene_parser.h"

#include <unistd.h>

#include <cstdio>
#include <string>

#include "test_util.h"

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

void testMissingFile() {
    SceneDocument doc;
    test::expect("missing", !parseSceneFile("/tmp/lwe_no_such_scene.json", doc), "a missing file fails");
}
}  // namespace

int main() {
    testObjects();
    testScriptedValues();
    testMissingFile();
    return test::finish("scene parser tests");
}
