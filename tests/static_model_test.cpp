#include <cjson/cJSON.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_util.h"
#include "wallpaper/2d/camera/perspective_camera.h"
#include "wallpaper/2d/layers/model/model_material.h"
#include "wallpaper/2d/layers/model/static_mesh.h"

namespace fs = std::filesystem;
using wallpaper_engine::MdlMesh;
using wallpaper_engine::MdlSubmesh;
using wallpaper_engine::MdlVertexAttribute;

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

// Clip-space depth (z / w) of a view-space point straight ahead of the camera.
float clipDepth(const mat4x4 projection, float view_z) {
    vec4 point = {0.0f, 0.0f, view_z, 1.0f};
    vec4 clip;
    mat4x4_mul_vec4(clip, projection, point);
    return clip[2] / clip[3];
}

void testPerspectiveDepthRange() {
    mat4x4 projection;
    perspectiveZeroToOne(projection, 90.0f, 2.0f, 0.5f, 100.0f);
    CHECK(near(clipDepth(projection, -0.5f), 0.0f));
    CHECK(near(clipDepth(projection, -100.0f), 1.0f));
    CHECK(clipDepth(projection, -10.0f) > 0.0f && clipDepth(projection, -10.0f) < 1.0f);
    CHECK(near(projection[0][0], 0.5f));  // 90 degrees: focal 1, divided by the aspect
    CHECK(near(projection[1][1], 1.0f));
    CHECK(near(projection[2][3], -1.0f));
}

void testCameraCentersItsTarget() {
    wallpaper_engine::SceneCameraDocument camera;
    camera.eye = {0.0f, 2.0f, 5.0f};
    camera.center = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    wallpaper_engine::SceneGeneralDocument general;
    PerspectiveCamera out;
    buildPerspectiveCamera(camera, general, 16.0f / 9.0f, out);

    vec4 target = {0.0f, 0.0f, 0.0f, 1.0f};
    vec4 clip;
    mat4x4_mul_vec4(clip, out.view_projection, target);
    CHECK(near(clip[0] / clip[3], 0.0f));
    CHECK(near(clip[1] / clip[3], 0.0f));
    CHECK(clip[2] / clip[3] > 0.0f && clip[2] / clip[3] < 1.0f);
    CHECK(near(out.eye[1], 2.0f) && near(out.eye[2], 5.0f));

    // A point above the target lands in the upper half of the screen.
    vec4 above = {0.0f, 0.5f, 0.0f, 1.0f};
    mat4x4_mul_vec4(clip, out.view_projection, above);
    CHECK(clip[1] / clip[3] > 0.0f);
}

void testCameraFallsBackToDefaultLens() {
    wallpaper_engine::SceneCameraDocument camera;
    wallpaper_engine::SceneGeneralDocument general;
    general.fov = 0.0f;
    general.near_z = -1.0f;
    general.far_z = 0.0f;
    PerspectiveCamera out;
    buildPerspectiveCamera(camera, general, 0.0f, out);

    mat4x4 expected;
    perspectiveZeroToOne(expected, kDefaultFovDegrees, 1.0f, kDefaultNearZ, kDefaultNearZ + kDefaultFarZ);
    bool same = true;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) same = same && near(out.projection[column][row], expected[column][row]);
    CHECK(same);
}

void testModelMatrix() {
    mat4x4 model;
    buildModelMatrix(model, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, (float)M_PI / 2.0f}, {2.0f, 2.0f, 2.0f});
    vec4 point = {1.0f, 0.0f, 0.0f, 1.0f};
    vec4 moved;
    mat4x4_mul_vec4(moved, model, point);
    // Scale to (2,0,0), turn about Z to (0,2,0), then move by the origin.
    CHECK(near(moved[0], 1.0f));
    CHECK(near(moved[1], 4.0f));
    CHECK(near(moved[2], 3.0f));

    buildModelMatrix(model, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    mat4x4 identity;
    mat4x4_identity(identity);
    bool same = true;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) same = same && near(model[column][row], identity[column][row]);
    CHECK(same);
}

void addElement(MdlMesh& mesh, MdlVertexAttribute attribute, uint32_t components) {
    mesh.elements.push_back({attribute, components, mesh.vertex_stride});
    mesh.vertex_stride += components;
}

void testPackPositionAndTexCoord() {
    MdlMesh mesh;
    addElement(mesh, MdlVertexAttribute::Position, 3);
    addElement(mesh, MdlVertexAttribute::TexCoord, 2);
    mesh.vertices = {1, 2, 3, 0.25f, 0.75f, 4, 5, 6, 0.5f, 1.0f};

    const std::vector<mesh_vertex_t> packed = packMeshVertices(mesh);
    CHECK(packed.size() == 2);
    CHECK(packed[1].position[0] == 4 && packed[1].position[1] == 5 && packed[1].position[2] == 6);
    CHECK(packed[0].uv[0] == 0.25f && packed[0].uv[1] == 0.75f);
    CHECK(packed[0].normal[0] == 0 && packed[0].normal[1] == 0 && packed[0].normal[2] == 0);
    CHECK(packed[0].tangent[0] == 0 && packed[0].tangent[3] == 0);
}

void testPackFullLayout() {
    MdlMesh mesh;
    addElement(mesh, MdlVertexAttribute::Position, 3);
    addElement(mesh, MdlVertexAttribute::Normal, 3);
    addElement(mesh, MdlVertexAttribute::Tangent, 4);
    addElement(mesh, MdlVertexAttribute::TexCoord, 4);  // four-float texcoords keep their first two
    mesh.vertices = {1, 2, 3, 0, 1, 0, 1, 0, 0, -1, 0.1f, 0.2f, 0.3f, 0.4f};

    const std::vector<mesh_vertex_t> packed = packMeshVertices(mesh);
    CHECK(packed.size() == 1);
    CHECK(packed[0].normal[1] == 1);
    CHECK(packed[0].tangent[0] == 1 && packed[0].tangent[3] == -1);
    CHECK(packed[0].uv[0] == 0.1f && packed[0].uv[1] == 0.2f);
    CHECK(packMeshVertices(MdlMesh{}).empty());
}

void testSubmeshRanges() {
    MdlSubmesh submesh;
    submesh.first_index = 3;
    submesh.index_count = 6;
    CHECK(submeshDrawable(submesh, 9));
    CHECK(!submeshDrawable(submesh, 8));
    submesh.index_count = 4;
    CHECK(!submeshDrawable(submesh, 100));
    submesh.index_count = 0;
    CHECK(!submeshDrawable(submesh, 100));
}

void testTranslucentDrawsGoLast() {
    std::vector<MeshDraw> draws = {{0, 3, 1}, {3, 3, 0}, {6, 3, 2}, {9, 3, 0}};
    orderMeshDraws(draws, {false, true, false});
    CHECK(draws[0].first_index == 3 && draws[1].first_index == 6 && draws[2].first_index == 9);
    CHECK(draws[3].first_index == 0);
}

MeshPipelineState stateOf(const char* json) {
    cJSON* pass = cJSON_Parse(json);
    const MeshPipelineState state = parseMeshPipelineState(pass);
    cJSON_Delete(pass);
    return state;
}

void testPipelineState() {
    const MeshPipelineState opaque = stateOf(R"({"shader":"car"})");
    CHECK(opaque.blend == ShaderBlendMode::Disabled);
    CHECK(opaque.cull_back_faces && opaque.depth_test && opaque.depth_write);

    const MeshPipelineState glass =
        stateOf(R"({"blending":"translucent","cullmode":"nocull","depthwrite":"disabled","depthtest":"disabled"})");
    CHECK(glass.blend == ShaderBlendMode::Alpha);
    CHECK(!glass.cull_back_faces && !glass.depth_test && !glass.depth_write);
    CHECK(stateOf(R"({"blending":"additive"})").blend == ShaderBlendMode::Additive);
    CHECK(stateOf(R"({"blending":"normal"})").blend == ShaderBlendMode::Disabled);
}

void testUserShaderValues() {
    const fs::path project = fs::temp_directory_path() / "static_model_test_project.json";
    std::ofstream(project) << R"({"general":{"properties":{
        "bodycolor":{"type":"color","value":"1 0 0"},
        "shine":{"type":"slider","value":0.5},
        "label":{"type":"textinput","value":"x"}}}})";
    UserProperties properties;
    properties.loadProject(project.string());
    fs::remove(project);

    cJSON* pass = cJSON_Parse(
        R"({"usershadervalues":{"bodycolor":"paintcolor","shine":"specularstrength","label":"x","missing":"y"}})");
    const auto values = resolveUserShaderValues(pass, properties);
    cJSON_Delete(pass);
    CHECK(values.size() == 2);
    CHECK(values.count("paintcolor") && values.at("paintcolor") == std::vector<float>({1.0f, 0.0f, 0.0f}));
    CHECK(values.count("specularstrength") && values.at("specularstrength") == std::vector<float>({0.5f}));
}

void testComboNames() {
    const auto combos = upperCaseCombos({{"metal", 1}, {"normalmap", 1}, {"TEX1FORMAT", 4}});
    CHECK(combos.size() == 3);
    CHECK(combos.count("METAL") && combos.count("NORMALMAP") && combos.at("TEX1FORMAT") == 4);
}

}  // namespace

int main() {
    testPerspectiveDepthRange();
    testCameraCentersItsTarget();
    testCameraFallsBackToDefaultLens();
    testModelMatrix();
    testPackPositionAndTexCoord();
    testPackFullLayout();
    testSubmeshRanges();
    testTranslucentDrawsGoLast();
    testPipelineState();
    testUserShaderValues();
    testComboNames();
    return test::finish("static_model_tests");
}
