// SceneScript runtime checks: the clock/date text pattern used by WE wallpapers.
#include "wallpaper/2d/script/scene_script.h"

#include <cstdio>
#include <string>

#include "test_util.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/2d/script/script_scene_backend.h"

namespace {

const char* kClockScript = R"JS(
export var scriptProperties = createScriptProperties()
    .addCheckbox({ name: 'use24hFormat', value: true })
    .addText({ name: 'delimiter', value: ':' })
    .finish();

export function update(value) {
    var months = ['Jan.', 'Feb.', 'Mar.', 'Apr.', 'May.', 'Jun.',
                  'Jul.', 'Aug.', 'Sep.', 'Oct.', 'Nov.', 'Dec.'];
    var time = new Date();
    var hours = time.getHours();
    var meridiem = '';
    if (!scriptProperties.use24hFormat) {
        meridiem = hours < 12 ? 'AM' : 'PM';
        hours %= 12;
        if (hours == 0) hours = 12;
    }
    hours = ('00' + hours).slice(-2);
    var minutes = ('00' + time.getMinutes()).slice(-2);
    var head = (scriptProperties.use24hFormat ? hours : meridiem + ' ' + hours)
        + scriptProperties.delimiter + minutes;
    return head + '\n' + months[time.getMonth()] + ' ' + time.getDate() + ' ' + time.getFullYear();
}
)JS";

class CountingScene : public ScriptSceneBackend {
   public:
    explicit CountingScene(size_t layers) : layers_(layers) {}
    bool layerExists(uint32_t) override {
        return false;
    }
    std::string layerName(uint32_t) override {
        return "";
    }
    bool getVector(uint32_t, const std::string&, double[3], int&) override {
        return false;
    }
    bool setVector(uint32_t, const std::string&, const double[3]) override {
        return false;
    }
    bool getBool(uint32_t, const std::string&, bool&) override {
        return false;
    }
    bool setBool(uint32_t, const std::string&, bool) override {
        return false;
    }
    uint32_t parentOf(uint32_t) override {
        return 0;
    }
    std::vector<uint32_t> childrenOf(uint32_t) override {
        return {};
    }
    uint32_t findLayerByName(const std::string&) override {
        return 0;
    }
    std::vector<uint32_t> allLayers() override {
        return std::vector<uint32_t>(layers_, 1);
    }

   private:
    size_t layers_;
};

// One layer (id 3) with effects "glow" and "ripple"; "ripple" owns the material constant "speed".
class EffectScene : public CountingScene {
   public:
    EffectScene() : CountingScene(1) {}
    bool visible[2] = {true, true};
    std::vector<double> speed = {1.0};

    bool layerExists(uint32_t id) override {
        return id == 3;
    }
    int effectCount(uint32_t id) override {
        return id == 3 ? 2 : 0;
    }
    int findEffect(uint32_t id, const std::string& name) override {
        if (id != 3) return -1;
        return name == "glow" ? 0 : name == "ripple" ? 1 : -1;
    }
    std::string effectName(uint32_t, int effect) override {
        return effect == 0 ? "glow" : "ripple";
    }
    bool effectVisible(uint32_t, int effect, bool& out) override {
        out = visible[effect];
        return true;
    }
    bool setEffectVisible(uint32_t, int effect, bool value) override {
        visible[effect] = value;
        return true;
    }
    std::vector<double> clear = {0.0, 0.0, 0.0};
    bool getSceneProperty(const std::string& name, std::vector<double>& out) override {
        if (name != "clearcolor") return false;
        out = clear;
        return true;
    }
    bool setSceneProperty(const std::string& name, const std::vector<double>& value) override {
        if (name != "clearcolor") return false;
        clear = value;
        return true;
    }
    std::string created_json;
    std::string sorted;
    std::string destroyed;
    uint32_t createLayer(const std::string& config) override {
        created_json = config;
        return 3;
    }
    bool destroyLayer(uint32_t id) override {
        destroyed = std::to_string(id);
        return true;
    }
    bool sortLayer(uint32_t id, int index) override {
        sorted = std::to_string(id) + "@" + std::to_string(index);
        return true;
    }
    double particle_rate = 1.0;
    std::string last_command;
    bool getNumber(uint32_t id, const std::string& property, double& out) override {
        if (id != 3 || property != "particle.rate") return false;
        out = particle_rate;
        return true;
    }
    bool setNumber(uint32_t, const std::string& property, double value) override {
        if (property != "particle.rate") return false;
        particle_rate = value;
        return true;
    }
    bool getBool(uint32_t, const std::string& property, bool& out) override {
        if (property != "video.playing") return false;
        out = true;
        return true;
    }
    bool layerCommand(uint32_t, const std::string& command) override {
        last_command = command;
        return true;
    }
    bool getMaterialProperty(uint32_t, int effect, const std::string& name, std::vector<double>& out) override {
        if (effect != 1 || name != "speed") return false;
        out = speed;
        return true;
    }
    bool setMaterialProperty(uint32_t, int effect, const std::string& name, const std::vector<double>& value) override {
        if (effect != 1 || name != "speed") return false;
        speed = value;
        return true;
    }
};

bool updateText(SceneScript& script, std::string& out) {
    ScriptValue value = ScriptValue::makeString("");
    if (!script.updateValue(value)) return false;
    out = value.text;
    return true;
}

}  // namespace

int main() {
    // 12h override, matching the workshop scene's user-bound property.
    {
        SceneScript script;
        const bool loaded =
            script.load(kClockScript, R"({"delimiter":":","use24hFormat":{"user":"_24hourformat","value":false}})");
        CHECK(loaded && script.valid());
        std::string out;
        CHECK(updateText(script, out));
        std::printf("12h clock => %s\n", out.c_str());
        CHECK(out.find("AM") != std::string::npos || out.find("PM") != std::string::npos);
        CHECK(out.find(".") != std::string::npos);  // month abbreviation
        CHECK(out.find('\n') != std::string::npos);
    }

    // Default (24h) leaves the meridiem off.
    {
        SceneScript script;
        CHECK(script.load(kClockScript, ""));
        std::string out;
        CHECK(updateText(script, out));
        std::printf("24h clock => %s\n", out.c_str());
        CHECK(out.find("AM") == std::string::npos);
        CHECK(out.find("PM") == std::string::npos);
    }

    // Event hooks: a direct call, a broadcast, a per-layer dispatch, and a sticky event replayed to a late script.
    {
        const char* source = R"JS(
let seen = 'none';
export function mediaPropertiesChanged(event) { seen = event.title + '|' + event.position.x; }
export function cursorClick(event) { seen = 'click ' + event.worldPosition.x; }
export function update(value) { return seen; }
)JS";
        SceneScript a, b;
        a.setLayerId(7);
        b.setLayerId(9);
        CHECK(a.load(source, "") && b.load(source, ""));
        std::string out;

        ScriptEvent media = {{"title", ScriptValue::makeString("Song")}, {"position", ScriptValue::makeVec2(3, 4)}};
        CHECK(a.callHook("mediaPropertiesChanged", media));
        CHECK(updateText(a, out) && out == "Song|3");
        CHECK(!a.callHook("noSuchHook", media));

        ScriptEngine& engine = ScriptEngine::instance();
        CHECK(engine.broadcast("mediaPropertiesChanged", {{"title", ScriptValue::makeString("All")},
                                                          {"position", ScriptValue::makeVec2(1, 2)}}) == 2);
        CHECK(updateText(b, out) && out == "All|1");

        CHECK(engine.dispatchToLayer(9, "cursorClick", {{"worldPosition", ScriptValue::makeVec2(55, 0)}}) == 1);
        CHECK(updateText(b, out) && out == "click 55");
        CHECK(updateText(a, out) && out == "All|1");  // layer 7 was not addressed

        engine.broadcast("mediaPropertiesChanged",
                         {{"title", ScriptValue::makeString("Sticky")}, {"position", ScriptValue::makeVec2(9, 9)}},
                         true);
        SceneScript late;
        CHECK(late.load(source, ""));
        engine.beginFrame(0.016, 1.0, 1920, 1080, 1920, 1080);  // delivers remembered events to new scripts
        CHECK(updateText(late, out) && out == "Sticky|9");
    }

    // Two scenes alive at once (a transition): each script sees its own scene and only the active scene gets events.
    {
        CountingScene first(2), second(5);
        ScriptEngine& engine = ScriptEngine::instance();
        const int first_scope = 0, second_scope = 0;
        engine.registerScope(&first_scope, &first);
        engine.registerScope(&second_scope, &second);
        const char* source = R"JS(
let clicks = 0;
export function cursorClick() { clicks++; }
export function update() { return thisScene.getLayerCount() + ':' + clicks; }
)JS";
        SceneScript a, b;
        a.setLayerId(7);
        b.setLayerId(7);  // object ids collide across wallpapers
        engine.setCreationScope(&first_scope);
        CHECK(a.load(source, ""));
        engine.setCreationScope(&second_scope);
        CHECK(b.load(source, ""));
        std::string out;

        engine.setActiveScope(&first_scope);
        CHECK(engine.dispatchToLayer(7, "cursorClick", {}) == 1);
        CHECK(updateText(a, out) && out == "2:1");
        CHECK(updateText(b, out) && out == "5:0");  // own backend even though the other scene is active

        engine.setActiveScope(&second_scope);
        CHECK(engine.broadcast("cursorClick", {}) == 1);
        CHECK(updateText(b, out) && out == "5:1");
        CHECK(updateText(a, out) && out == "2:1");

        // `shared` is separate per scene.
        const char* shared_source = R"JS(
export function init() { if (shared.mark === undefined) shared.mark = thisScene.getLayerCount(); }
export function update() { return String(shared.mark); }
)JS";
        SceneScript sa, sb;
        engine.setCreationScope(&first_scope);
        CHECK(sa.load(shared_source, ""));
        engine.setCreationScope(&second_scope);
        CHECK(sb.load(shared_source, ""));
        ScriptValue unused = ScriptValue::makeString("");
        sa.initValue(unused);
        sb.initValue(unused);
        CHECK(updateText(sa, out) && out == "2");
        CHECK(updateText(sb, out) && out == "5");

        engine.unregisterScope(&first_scope);
        engine.unregisterScope(&second_scope);
        engine.setCreationScope(nullptr);
        engine.setActiveScope(nullptr);
    }

    // Effects: look up by name or index, toggle visibility, read and write material constants.
    {
        EffectScene scene;
        ScriptEngine& engine = ScriptEngine::instance();
        const int scope = 0;
        engine.registerScope(&scope, &scene);
        engine.setCreationScope(&scope);
        const char* source = R"JS(
export function update() {
    var glow = thisLayer.getEffect('glow');
    var ripple = thisLayer.getEffect(1);
    glow.visible = false;
    ripple.setMaterialProperty('speed', ripple.getMaterialProperty('speed') * 3);
    return thisLayer.getEffectCount() + ':' + glow.name + ':' + ripple.name + ':' + (thisLayer.getEffect('none') === undefined);
}
)JS";
        SceneScript script;
        script.setLayerId(3);
        CHECK(script.load(source, ""));
        std::string out;
        CHECK(updateText(script, out) && out == "2:glow:ripple:true");
        CHECK(!scene.visible[0] && scene.visible[1]);
        CHECK(scene.speed.size() == 1 && scene.speed[0] == 3.0);

        // Particle systems: scalar fields and commands reach the backend.
        SceneScript particles;
        particles.setLayerId(3);
        CHECK(particles.load(R"JS(
export function update() {
    var system = thisLayer.getParticleSystem();
    system.rate = system.rate * 2;
    system.emitParticles(5);
    return String(system.getInstanceCount());
})JS",
                             ""));
        CHECK(updateText(particles, out) && out == "1");
        CHECK(scene.particle_rate == 2.0 && scene.last_command == "particle.emit:5");

        // Dynamic layers: the config reaches the scene as scene.json text (vectors as "x y z").
        SceneScript dynamic;
        dynamic.setLayerId(3);
        CHECK(dynamic.load(R"JS(
export function update() {
    var layer = thisScene.createLayer({ image: 'models/x.json', origin: { x: 1, y: 2, z: 3 }, name: 'made' });
    thisScene.sortLayer(layer, 0);
    thisScene.destroyLayer(layer);
    return String(layer && layer.name !== undefined);
})JS",
                           ""));
        CHECK(updateText(dynamic, out) && out == "true");
        CHECK(scene.created_json.find("\"origin\":\"1 2 3\"") != std::string::npos);
        CHECK(scene.sorted == "3@0" && scene.destroyed == "3");

        // Scene settings read and write through the backend; a number fills every component of a color.
        SceneScript settings;
        settings.setLayerId(3);
        CHECK(settings.load(R"JS(
export function update() {
    thisScene.clearcolor = 0.5;
    return String(thisScene.clearcolor.y);
})JS",
                            ""));
        CHECK(updateText(settings, out) && out == "0.5");
        CHECK(scene.clear.size() == 3 && scene.clear[2] == 0.5);

        // Video textures: playback commands.
        SceneScript video;
        video.setLayerId(3);
        CHECK(video.load(R"JS(
export function update() {
    var texture = thisLayer.getVideoTexture();
    texture.pause();
    return String(texture.isPlaying());
})JS",
                         ""));
        CHECK(updateText(video, out) && out == "true");
        CHECK(scene.last_command == "video.pause");

        // A script bound to an effect sees that effect as thisObject.
        SceneScript bound;
        bound.setLayerId(3);
        bound.setProperty("effect:1:visible");
        CHECK(bound.load("export function update(v) { return thisObject.name === 'ripple'; }", ""));
        ScriptValue flag = ScriptValue::makeBool(false);
        CHECK(bound.updateValue(flag) && flag.number == 1.0);

        engine.unregisterScope(&scope);
        engine.setCreationScope(nullptr);
    }

    return test::finish("scene script tests");
}
