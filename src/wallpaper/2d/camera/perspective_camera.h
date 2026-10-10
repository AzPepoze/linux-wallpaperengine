#ifndef PERSPECTIVE_CAMERA_H
#define PERSPECTIVE_CAMERA_H

#include <array>

#include "linmath.h"
#include "wallpaper/2d/parser/scene_document.h"

// Fallbacks for 3D scenes whose `general` block leaves the lens out or sets it out of range.
constexpr float kDefaultFovDegrees = 50.0f;
constexpr float kDefaultNearZ = 0.01f;
constexpr float kDefaultFarZ = 10000.0f;

// The fixed camera a 3D scene is drawn with.
struct PerspectiveCamera {
    mat4x4 view;
    mat4x4 projection;
    mat4x4 view_projection;
    vec3 eye;
};

// Right-handed perspective with a vertical field of view and Vulkan's 0..1 clip depth.
void perspectiveZeroToOne(mat4x4 out, float fov_y_degrees, float aspect, float near_z, float far_z);

// Uses the scene's top-level camera (eye, center, up) and the lens from `general`.
void buildPerspectiveCamera(const wallpaper_engine::SceneCameraDocument& camera,
                            const wallpaper_engine::SceneGeneralDocument& general, float aspect,
                            PerspectiveCamera& out);

// Mirror across the horizontal plane y = plane_y; it reverses triangle winding.
void floorMirror(mat4x4 out, float plane_y);

// The camera seen in a floor mirror at y = plane_y. Its clip y is flipped as well, so the image is stored
// bottom-up and the two winding flips cancel: the scene pipelines draw it unchanged.
void buildReflectedCamera(const PerspectiveCamera& camera, float plane_y, PerspectiveCamera& out);

// Translation, then rotation about Z, Y and X (radians), then scale.
void buildModelMatrix(mat4x4 out, const std::array<float, 3>& origin, const std::array<float, 3>& angles,
                      const std::array<float, 3>& scale);

#endif  // PERSPECTIVE_CAMERA_H
