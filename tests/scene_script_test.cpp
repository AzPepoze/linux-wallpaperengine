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

    return test::finish("scene script tests");
}
