// SceneScript runtime checks: the clock/date text pattern used by WE wallpapers.
#include "wallpaper/2d/script/scene_script.h"

#include <cassert>
#include <cstdio>
#include <string>

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

}  // namespace

int main() {
    // 12h override, matching the workshop scene's user-bound property.
    {
        SceneScript script;
        const bool loaded =
            script.load(kClockScript, R"({"delimiter":":","use24hFormat":{"user":"_24hourformat","value":false}})");
        assert(loaded && script.valid());
        std::string out;
        assert(script.update("", out));
        std::printf("12h clock => %s\n", out.c_str());
        assert(out.find("AM") != std::string::npos || out.find("PM") != std::string::npos);
        assert(out.find(".") != std::string::npos);  // month abbreviation
        assert(out.find('\n') != std::string::npos);
    }

    // Default (24h) leaves the meridiem off.
    {
        SceneScript script;
        assert(script.load(kClockScript, ""));
        std::string out;
        assert(script.update("", out));
        std::printf("24h clock => %s\n", out.c_str());
        assert(out.find("AM") == std::string::npos);
        assert(out.find("PM") == std::string::npos);
    }

    std::printf("scene script tests passed\n");
    return 0;
}
