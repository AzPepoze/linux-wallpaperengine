// SceneScript runtime checks: the clock/date text pattern used by WE wallpapers.
#include "wallpaper/2d/script/scene_script.h"

#include <cstdio>
#include <string>

#include "test_util.h"
#include "wallpaper/2d/script/script_engine.h"

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

    return test::finish("scene script tests");
}
