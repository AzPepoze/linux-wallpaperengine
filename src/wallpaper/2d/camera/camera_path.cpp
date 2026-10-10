#include "camera_path.h"

#include <math.h>

using wallpaper_engine::CameraPathDocument;
using wallpaper_engine::CameraTransformDocument;

namespace {

bool isPlayable(const CameraPathDocument& path) {
    return path.duration > 0.0f && !path.transforms.empty();
}

float totalDuration(const std::vector<CameraPathDocument>& paths) {
    float total = 0.0f;
    for (const CameraPathDocument& path : paths)
        if (isPlayable(path)) total += path.duration;
    return total;
}

// The playable path at `seconds` into one loop, with `seconds` made local to it.
const CameraPathDocument* pathAt(const std::vector<CameraPathDocument>& paths, float& seconds) {
    const CameraPathDocument* last = nullptr;
    for (const CameraPathDocument& path : paths) {
        if (!isPlayable(path)) continue;
        if (seconds < path.duration) return &path;
        seconds -= path.duration;
        last = &path;
    }
    seconds = last ? last->duration : 0.0f;  // float rounding at the very end of the loop
    return last;
}

std::array<float, 3> lerp(const std::array<float, 3>& from, const std::array<float, 3>& to, float t) {
    return {from[0] + (to[0] - from[0]) * t, from[1] + (to[1] - from[1]) * t, from[2] + (to[2] - from[2]) * t};
}

CameraPose poseOf(const CameraTransformDocument& transform) {
    return {transform.eye, transform.center, transform.up};
}

CameraPose blend(const CameraTransformDocument& from, const CameraTransformDocument& to, float seconds) {
    const float span = to.timestamp - from.timestamp;
    if (span <= 0.0f) return poseOf(to);
    const float t = (seconds - from.timestamp) / span;
    return {lerp(from.eye, to.eye, t), lerp(from.center, to.center, t), lerp(from.up, to.up, t)};
}

// Keyframes are in timestamp order; times outside them hold the nearest one.
CameraPose samplePath(const CameraPathDocument& path, float seconds) {
    const std::vector<CameraTransformDocument>& keys = path.transforms;
    if (seconds <= keys.front().timestamp) return poseOf(keys.front());
    for (size_t i = 1; i < keys.size(); ++i)
        if (seconds <= keys[i].timestamp) return blend(keys[i - 1], keys[i], seconds);
    return poseOf(keys.back());
}

}  // namespace

bool sampleCameraPaths(const std::vector<CameraPathDocument>& paths, float seconds, CameraPose& out) {
    const float total = totalDuration(paths);
    if (total <= 0.0f) return false;
    float local = fmodf(fmaxf(seconds, 0.0f), total);
    const CameraPathDocument* path = pathAt(paths, local);
    out = samplePath(*path, local);
    return true;
}

void followCameraPaths(wallpaper_engine::SceneCameraDocument& camera, float seconds) {
    CameraPose pose;
    if (!sampleCameraPaths(camera.paths, seconds, pose)) return;
    camera.eye = pose.eye;
    camera.center = pose.center;
    camera.up = pose.up;
}
