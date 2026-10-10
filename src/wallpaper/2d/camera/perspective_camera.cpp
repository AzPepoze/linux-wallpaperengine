#include "perspective_camera.h"

#include <math.h>

namespace {

float lensFov(float fov_degrees) {
    return fov_degrees > 0.0f && fov_degrees < 180.0f ? fov_degrees : kDefaultFovDegrees;
}

float lensNear(float near_z) {
    return near_z > 0.0f ? near_z : kDefaultNearZ;
}

float lensFar(float far_z, float near_z) {
    return far_z > near_z ? far_z : near_z + kDefaultFarZ;
}

void toVec3(vec3 out, const std::array<float, 3>& value) {
    out[0] = value[0];
    out[1] = value[1];
    out[2] = value[2];
}

}  // namespace

void perspectiveZeroToOne(mat4x4 out, float fov_y_degrees, float aspect, float near_z, float far_z) {
    const float focal = 1.0f / tanf(fov_y_degrees * (float)M_PI / 360.0f);
    const float depth_range = near_z - far_z;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) out[column][row] = 0.0f;
    out[0][0] = focal / aspect;
    out[1][1] = focal;
    out[2][2] = far_z / depth_range;
    out[2][3] = -1.0f;
    out[3][2] = near_z * far_z / depth_range;
}

void buildPerspectiveCamera(const wallpaper_engine::SceneCameraDocument& camera,
                            const wallpaper_engine::SceneGeneralDocument& general, float aspect,
                            PerspectiveCamera& out) {
    vec3 center, up;
    toVec3(out.eye, camera.eye);
    toVec3(center, camera.center);
    toVec3(up, camera.up);
    mat4x4_look_at(out.view, out.eye, center, up);

    const float near_z = lensNear(general.near_z);
    const float safe_aspect = aspect > 0.0f ? aspect : 1.0f;
    perspectiveZeroToOne(out.projection, lensFov(general.fov), safe_aspect, near_z, lensFar(general.far_z, near_z));
    mat4x4_mul(out.view_projection, out.projection, out.view);
}

void buildModelMatrix(mat4x4 out, const std::array<float, 3>& origin, const std::array<float, 3>& angles,
                      const std::array<float, 3>& scale) {
    mat4x4_translate(out, origin[0], origin[1], origin[2]);
    mat4x4_rotate_Z(out, out, angles[2]);
    mat4x4_rotate_Y(out, out, angles[1]);
    mat4x4_rotate_X(out, out, angles[0]);
    mat4x4_scale_aniso(out, out, scale[0], scale[1], scale[2]);
}
