#include "wallpaper/2d/script/media_events.h"

#include <cstring>
#include <memory>
#include <string>

#include "shared/media/fake_media_source.h"
#include "test_util.h"
#include "wallpaper/2d/script/script_engine.h"
using test::expect;

using namespace wallpaper_engine;

namespace {

const ScriptValue* field(const ScriptEvent& event, const char* name) {
    for (const auto& [key, value] : event)
        if (key == name) return &value;
    return nullptr;
}

bool updateText(SceneScript& script, std::string& out) {
    ScriptValue value = ScriptValue::makeString("");
    if (!script.updateValue(value)) return false;
    out = value.text;
    return true;
}

bool hasString(const ScriptEvent& event, const char* name, const char* expected) {
    const ScriptValue* value = field(event, name);
    return value && value->kind == ScriptValue::Kind::String && value->text == expected;
}

void testStatusMapping() {
    MediaEvent event;
    event.kind = MediaEvent::Kind::Status;
    event.enabled = true;
    const char* hook = nullptr;
    bool sticky = false;
    const ScriptEvent script = toScriptEvent(event, hook, sticky);
    expect("status", std::strcmp(hook, "mediaStatusChanged") == 0, "hook name");
    expect("status", sticky, "sticky");
    expect("status", script.size() == 1, "single field");
    const ScriptValue* enabled = field(script, "enabled");
    expect("status", enabled && enabled->kind == ScriptValue::Kind::Bool && enabled->number == 1.0, "enabled bool");
}

void testPlaybackMapping() {
    const PlaybackState states[] = {PlaybackState::Stopped, PlaybackState::Playing, PlaybackState::Paused};
    const double expected[] = {0.0, 1.0, 2.0};
    for (int i = 0; i < 3; ++i) {
        MediaEvent event;
        event.kind = MediaEvent::Kind::Playback;
        event.state = states[i];
        const char* hook = nullptr;
        bool sticky = false;
        const ScriptEvent script = toScriptEvent(event, hook, sticky);
        expect("playback", std::strcmp(hook, "mediaPlaybackChanged") == 0, "hook name");
        expect("playback", sticky, "sticky");
        const ScriptValue* state = field(script, "state");
        expect("playback", state && state->kind == ScriptValue::Kind::Number && state->number == expected[i], "state");
    }
}

void testPropertiesMapping() {
    MediaEvent event;
    event.kind = MediaEvent::Kind::Properties;
    event.properties = {"T", "A", "AT", "AA", "S", "G", "C"};
    const char* hook = nullptr;
    bool sticky = false;
    const ScriptEvent script = toScriptEvent(event, hook, sticky);
    expect("properties", std::strcmp(hook, "mediaPropertiesChanged") == 0, "hook name");
    expect("properties", sticky, "sticky");
    expect("properties", script.size() == 7, "seven fields");
    expect("properties", hasString(script, "title", "T"), "title");
    expect("properties", hasString(script, "artist", "A"), "artist");
    expect("properties", hasString(script, "albumTitle", "AT"), "albumTitle");
    expect("properties", hasString(script, "albumArtist", "AA"), "albumArtist");
    expect("properties", hasString(script, "subTitle", "S"), "subTitle");
    expect("properties", hasString(script, "genres", "G"), "genres");
    expect("properties", hasString(script, "contentType", "C"), "contentType");
}

void testThumbnailMapping() {
    MediaEvent event;
    event.kind = MediaEvent::Kind::Thumbnail;
    event.thumbnail.has_thumbnail = true;
    event.thumbnail.primary[0] = 0.25f;
    event.thumbnail.secondary[0] = 0.5f;
    event.thumbnail.tertiary[0] = 0.75f;
    event.thumbnail.text[0] = 0.5f;
    event.thumbnail.high_contrast[0] = 0.75f;
    const char* hook = nullptr;
    bool sticky = false;
    const ScriptEvent script = toScriptEvent(event, hook, sticky);
    expect("thumbnail", std::strcmp(hook, "mediaThumbnailChanged") == 0, "hook name");
    expect("thumbnail", sticky, "sticky");
    const ScriptValue* has = field(script, "hasThumbnail");
    expect("thumbnail", has && has->kind == ScriptValue::Kind::Bool && has->number == 1.0, "hasThumbnail");
    const ScriptValue* primary = field(script, "primaryColor");
    expect("thumbnail", primary && primary->kind == ScriptValue::Kind::Vec3 && primary->vec[0] == 0.25, "primary");
    expect("thumbnail", field(script, "secondaryColor")->vec[0] == 0.5, "secondary");
    expect("thumbnail", field(script, "tertiaryColor")->vec[0] == 0.75, "tertiary");
    expect("thumbnail", field(script, "textColor")->vec[0] == 0.5, "text");
    expect("thumbnail", field(script, "highContrastColor")->vec[0] == 0.75, "high contrast");

    event.thumbnail = {};
    const ScriptEvent none = toScriptEvent(event, hook, sticky);
    expect("thumbnail", !field(none, "hasThumbnail")->number, "no thumbnail flag");
    expect("thumbnail", field(none, "primaryColor")->vec[0] == 0.0 && field(none, "textColor")->vec[0] == 0.0,
           "colors zero without a thumbnail");
}

void testTimelineMapping() {
    MediaEvent event;
    event.kind = MediaEvent::Kind::Timeline;
    event.position = 12.5;
    event.duration = 180.0;
    const char* hook = nullptr;
    bool sticky = true;
    const ScriptEvent script = toScriptEvent(event, hook, sticky);
    expect("timeline", std::strcmp(hook, "mediaTimelineChanged") == 0, "hook name");
    expect("timeline", !sticky, "not sticky");
    expect("timeline", field(script, "position")->number == 12.5, "position");
    expect("timeline", field(script, "duration")->number == 180.0, "duration");
}

const char* kMediaScript = R"JS(
let state = { title: 'unset', primaryX: -1, playback: -1 };
export function mediaPropertiesChanged(event) { state.title = event.title; }
export function mediaThumbnailChanged(event) { state.primaryX = event.primaryColor.x; }
export function mediaPlaybackChanged(event) { state.playback = event.state; }
export function update(value) { return state.title + '|' + state.primaryX + '|' + state.playback; }
)JS";

void testBridgeDeliversEvents() {
    auto source = std::make_unique<FakeMediaSource>();
    FakeMediaSource* fake = source.get();
    MediaScriptBridge bridge(std::move(source));

    SceneScript script;
    CHECK(script.load(kMediaScript, ""));
    CHECK(script.valid());

    bridge.update();  // the script exports media hooks, so the source starts; there is nothing queued yet

    MediaEvent properties;
    properties.kind = MediaEvent::Kind::Properties;
    properties.properties.title = "Synthetic Song";
    fake->push(properties);
    MediaEvent playback;
    playback.kind = MediaEvent::Kind::Playback;
    playback.state = PlaybackState::Playing;
    fake->push(playback);
    MediaEvent thumbnail;
    thumbnail.kind = MediaEvent::Kind::Thumbnail;
    thumbnail.thumbnail.has_thumbnail = true;
    thumbnail.thumbnail.primary[0] = 0.25f;
    fake->push(thumbnail);

    bridge.update();

    std::string out;
    CHECK(updateText(script, out));
    CHECK(out == "Synthetic Song|0.25|1");
}

void testStickyReplayedToLateScript() {
    auto source = std::make_unique<FakeMediaSource>();
    FakeMediaSource* fake = source.get();
    MediaScriptBridge bridge(std::move(source));

    SceneScript early;
    CHECK(early.load(kMediaScript, ""));
    bridge.update();

    MediaEvent properties;
    properties.kind = MediaEvent::Kind::Properties;
    properties.properties.title = "Late Song";
    fake->push(properties);
    MediaEvent playback;
    playback.kind = MediaEvent::Kind::Playback;
    playback.state = PlaybackState::Paused;
    fake->push(playback);
    MediaEvent thumbnail;
    thumbnail.kind = MediaEvent::Kind::Thumbnail;
    thumbnail.thumbnail.has_thumbnail = true;
    thumbnail.thumbnail.primary[0] = 0.5f;
    fake->push(thumbnail);
    bridge.update();

    SceneScript late;
    CHECK(late.load(kMediaScript, ""));
    ScriptEngine::instance().beginFrame(0.016, 1.0, 1920, 1080, 1920, 1080);

    std::string out;
    CHECK(updateText(late, out));
    CHECK(out == "Late Song|0.5|2");
}

}  // namespace

int main() {
    testStatusMapping();
    testPlaybackMapping();
    testPropertiesMapping();
    testThumbnailMapping();
    testTimelineMapping();
    testBridgeDeliversEvents();
    testStickyReplayedToLateScript();
    return test::finish("media events tests");
}
