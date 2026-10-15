#ifndef CAMERA_PATH_H
#define CAMERA_PATH_H

#include <array>
#include <vector>

#include "wallpaper/2d/parser/scene_document.h"

struct CameraPose {
    std::array<float, 3> eye = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> center = {0.0f, 0.0f, -1.0f};
    std::array<float, 3> up = {0.0f, 1.0f, 0.0f};
};

// Plays the paths one after another and loops, interpolating linearly between keyframes.
// False when no path has a duration and a keyframe, so the caller keeps its fixed camera.
bool sampleCameraPaths(const std::vector<wallpaper_engine::CameraPathDocument>& paths, float seconds,
                       CameraPose& out);

// Moves the scene camera along its paths; a camera without playable paths is left as authored.
void followCameraPaths(wallpaper_engine::SceneCameraDocument& camera, float seconds);

#endif  // CAMERA_PATH_H
