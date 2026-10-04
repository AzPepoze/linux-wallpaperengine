// AnimationTimelines: shared per-object clocks, playback control and end events (synthetic keyframes only).
#include "wallpaper/2d/animation/animation_timelines.h"

#include <cmath>
#include <map>

#include "test_util.h"

namespace {

using wallpaper_engine::CurveKeyframe;
using wallpaper_engine::PropertyAnimationDocument;

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-4;
}

PropertyAnimationDocument makeAnimation(const std::string& property, const std::string& mode,
                                        std::vector<CurveKeyframe> keys, float fps = 10.0f, float length = 10.0f) {
    PropertyAnimationDocument animation;
    animation.property = property;
    animation.curves[0].keys = std::move(keys);
    animation.curves[0].fps = fps;
    animation.curves[0].length = length;
    animation.curves[0].mode = mode;
    return animation;
}

// Records the last value reported per "object:property".
struct Recorder {
    std::map<std::string, double> last;
    std::function<void(const AnimatedValue&)> callback() {
        return [this](const AnimatedValue& value) {
            last[std::to_string(value.object_id) + ":" + value.property] = value.value[0];
        };
    }
};

void testSharedTimeline() {
    // scale is the root and lists alpha; both ramp 0 -> 10 over 10 frames at 10 fps (1 second).
    PropertyAnimationDocument scale = makeAnimation("scale", "loop", {{0, 0}, {10, 10}});
    scale.name = "Pulse";
    scale.children = {"alpha"};
    PropertyAnimationDocument alpha = makeAnimation("alpha", "loop", {{0, 0}, {10, 20}});
    alpha.parent = "scale";

    AnimationTimelines timelines;
    timelines.add(7, {scale, alpha});
    test::expect("shared", timelines.size() == 1, "root and child share one timeline");

    Recorder recorder;
    timelines.update(0.5f, recorder.callback());
    test::expect("shared", near(recorder.last["7:scale"], 5.0) && near(recorder.last["7:alpha"], 10.0),
                 "both channels read the same clock");

    const uint32_t handle = timelines.find(7, "Pulse");
    test::expect("shared", handle != 0 && handle == timelines.find(7, "alpha") && handle == timelines.find(7, ""),
                 "found by name, by property and as the first timeline");
    test::expect("shared", timelines.find(7, "origin") == 0 && timelines.find(8, "") == 0, "unknown keys are 0");
    std::string name;
    test::expect("shared", timelines.getString(handle, "name", name) && name == "Pulse", "name field");

    double value = 0;
    test::expect("shared",
                 timelines.get(handle, "fps", value) && near(value, 10) && timelines.get(handle, "frameCount", value) &&
                     near(value, 10) && timelines.get(handle, "duration", value) && near(value, 1.0),
                 "fps, frameCount and duration");
    test::expect("shared", timelines.get(handle, "frame", value) && near(value, 5.0), "frame follows the clock");
}

void testRateAndControl() {
    AnimationTimelines timelines;
    timelines.add(1, {makeAnimation("alpha", "loop", {{0, 0}, {10, 10}})});
    const uint32_t handle = timelines.find(1, "alpha");
    Recorder recorder;

    test::expect("control", timelines.set(handle, "rate", 2.0), "rate can be set");
    timelines.update(0.25f, recorder.callback());
    test::expect("control", near(recorder.last["1:alpha"], 5.0), "rate 2 doubles the speed");

    test::expect("control", timelines.command(handle, "pause"), "pause");
    timelines.update(0.25f, recorder.callback());
    test::expect("control", near(recorder.last["1:alpha"], 5.0), "a paused timeline holds its value");
    double playing = 1;
    test::expect("control", timelines.get(handle, "playing", playing) && playing == 0.0, "playing reads false");

    test::expect("control", timelines.set(handle, "frame", 2.0), "seek");
    timelines.update(0.0f, recorder.callback());
    test::expect("control", near(recorder.last["1:alpha"], 2.0), "seeking moves the value while paused");

    test::expect("control", timelines.command(handle, "play"), "play");
    timelines.update(0.1f, recorder.callback());
    test::expect("control", near(recorder.last["1:alpha"], 4.0), "playing resumes from the seek position");

    test::expect("control", timelines.command(handle, "stop"), "stop");
    timelines.update(0.0f, recorder.callback());
    test::expect("control", near(recorder.last["1:alpha"], 0.0), "stop returns to the beginning");
    test::expect("control", !timelines.command(handle, "explode") && !timelines.command(999, "play"),
                 "unknown command or handle fails");
}

void testStartPausedAndSingle() {
    PropertyAnimationDocument paused = makeAnimation("alpha", "single", {{0, 0}, {10, 10}});
    paused.start_paused = true;
    AnimationTimelines timelines;
    timelines.add(3, {paused});
    const uint32_t handle = timelines.find(3, "");
    Recorder recorder;

    timelines.update(0.5f, recorder.callback());
    test::expect("single", near(recorder.last["3:alpha"], 0.0), "start-paused timelines wait for play()");
    timelines.command(handle, "play");
    timelines.update(0.4f, recorder.callback());
    test::expect("single", near(recorder.last["3:alpha"], 4.0) && timelines.takeEnded().empty(),
                 "plays toward the end");
    timelines.update(2.0f, recorder.callback());
    test::expect("single", near(recorder.last["3:alpha"], 10.0), "single mode holds the last frame");
    const std::vector<uint32_t> ended = timelines.takeEnded();
    test::expect("single", ended.size() == 1 && ended[0] == handle, "the end is reported once");
    test::expect("single", timelines.takeEnded().empty(), "and then cleared");

    timelines.command(handle, "play");  // finished single animation restarts
    timelines.update(0.1f, recorder.callback());
    test::expect("single", near(recorder.last["3:alpha"], 1.0), "play() after the end restarts it");
}

void testOrphanAndRelative() {
    // A child whose root is missing still animates on its own timeline; relative is reported through.
    PropertyAnimationDocument orphan = makeAnimation("origin", "loop", {{0, 0}, {10, 10}});
    orphan.parent = "scale";
    orphan.relative = true;
    AnimationTimelines timelines;
    timelines.add(5, {orphan});
    test::expect("orphan", timelines.size() == 1 && timelines.find(5, "origin") != 0, "orphan gets its own timeline");

    bool relative = false;
    timelines.update(0.1f, [&](const AnimatedValue& value) { relative = value.relative && value.has[0]; });
    test::expect("orphan", relative, "the relative flag and populated channel are reported");
}

}  // namespace

int main() {
    testSharedTimeline();
    testRateAndControl();
    testStartPausedAndSingle();
    testOrphanAndRelative();
    return test::finish("animation timelines tests");
}
