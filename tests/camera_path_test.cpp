#include <cmath>
#include <vector>

#include "test_util.h"
#include "wallpaper/2d/camera/camera_path.h"

using wallpaper_engine::CameraPathDocument;
using wallpaper_engine::CameraTransformDocument;
using wallpaper_engine::SceneCameraDocument;

namespace {

bool near(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return std::fabs(a[0] - b[0]) < 1e-3f && std::fabs(a[1] - b[1]) < 1e-3f && std::fabs(a[2] - b[2]) < 1e-3f;
}

CameraTransformDocument key(float timestamp, float eye_x, float center_x, float up_x) {
    CameraTransformDocument out;
    out.timestamp = timestamp;
    out.eye = {eye_x, 1.0f, 2.0f};
    out.center = {center_x, 0.0f, 0.0f};
    out.up = {up_x, 1.0f, 0.0f};
    return out;
}

CameraPathDocument path(float duration, float start_x, float end_x) {
    CameraPathDocument out;
    out.duration = duration;
    out.transforms = {key(0.0f, start_x, start_x, 0.0f), key(duration, end_x, end_x, 1.0f)};
    return out;
}

// Path 0 runs eye.x 0 -> 10 over 10 s, path 1 runs 100 -> 120 over 20 s.
std::vector<CameraPathDocument> twoPaths() {
    return {path(10.0f, 0.0f, 10.0f), path(20.0f, 100.0f, 120.0f)};
}

void testKeyframesAreExact() {
    const std::vector<CameraPathDocument> paths = {path(10.0f, 0.0f, 10.0f)};
    CameraPose pose;
    CHECK(sampleCameraPaths(paths, 0.0f, pose));
    CHECK(near(pose.eye, {0.0f, 1.0f, 2.0f}));
    CHECK(near(pose.center, {0.0f, 0.0f, 0.0f}));
    CHECK(near(pose.up, {0.0f, 1.0f, 0.0f}));

    CHECK(sampleCameraPaths(paths, 9.9999f, pose));
    CHECK(near(pose.eye, {10.0f, 1.0f, 2.0f}));
    CHECK(near(pose.center, {10.0f, 0.0f, 0.0f}));
    CHECK(near(pose.up, {1.0f, 1.0f, 0.0f}));
}

void testMidpointIsLinear() {
    CameraPose pose;
    CHECK(sampleCameraPaths(twoPaths(), 5.0f, pose));
    CHECK(near(pose.eye, {5.0f, 1.0f, 2.0f}));
    CHECK(near(pose.up, {0.5f, 1.0f, 0.0f}));
}

void testNextPathStartsAtTheDuration() {
    CameraPose pose;
    CHECK(sampleCameraPaths(twoPaths(), 10.0f, pose));
    CHECK(near(pose.eye, {100.0f, 1.0f, 2.0f}));
    CHECK(sampleCameraPaths(twoPaths(), 20.0f, pose));
    CHECK(near(pose.eye, {110.0f, 1.0f, 2.0f}));
}

void testLoopsAfterTheLastPath() {
    CameraPose pose;
    CHECK(sampleCameraPaths(twoPaths(), 30.0f, pose));
    CHECK(near(pose.eye, {0.0f, 1.0f, 2.0f}));
    CHECK(sampleCameraPaths(twoPaths(), 65.0f, pose));  // second loop, 5 s in
    CHECK(near(pose.eye, {5.0f, 1.0f, 2.0f}));
}

void testEmptyAndUnplayablePaths() {
    CameraPose pose;
    CHECK(!sampleCameraPaths({}, 3.0f, pose));

    CameraPathDocument no_keys;
    no_keys.duration = 5.0f;
    CameraPathDocument no_duration = path(0.0f, 7.0f, 7.0f);
    CHECK(!sampleCameraPaths({no_keys, no_duration}, 3.0f, pose));

    // Unplayable paths are skipped, not given time.
    CHECK(sampleCameraPaths({no_keys, path(10.0f, 0.0f, 10.0f)}, 2.0f, pose));
    CHECK(near(pose.eye, {2.0f, 1.0f, 2.0f}));
}

void testFollowLeavesCameraWithoutPaths() {
    SceneCameraDocument camera;
    camera.eye = {1.0f, 2.0f, 3.0f};
    followCameraPaths(camera, 4.0f);
    CHECK(near(camera.eye, {1.0f, 2.0f, 3.0f}));

    camera.paths = twoPaths();
    followCameraPaths(camera, 15.0f);
    CHECK(near(camera.eye, {105.0f, 1.0f, 2.0f}));
    CHECK(near(camera.center, {105.0f, 0.0f, 0.0f}));
}

}  // namespace

int main() {
    testKeyframesAreExact();
    testMidpointIsLinear();
    testNextPathStartsAtTheDuration();
    testLoopsAfterTheLastPath();
    testEmptyAndUnplayablePaths();
    testFollowLeavesCameraWithoutPaths();
    return test::finish("camera_path_tests");
}
