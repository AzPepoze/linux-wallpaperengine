#include "wallpaper/2d/puppet/mdl_mesh.h"

#include <stdint.h>
#include <string.h>

#include <vector>

#include "../test_util.h"
using test::check;

using namespace wallpaper_engine;

namespace {

constexpr uint32_t kPositionTexCoord = 0x09;
constexpr uint32_t kFullFormat = 0x0f;

struct Blob {
    std::vector<uint8_t> bytes;

    void u8(uint8_t value) {
        bytes.push_back(value);
    }
    void u16(uint16_t value) {
        bytes.push_back((uint8_t)(value & 0xff));
        bytes.push_back((uint8_t)(value >> 8));
    }
    void u32(uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes.push_back((uint8_t)((value >> (i * 8)) & 0xff));
    }
    void f32(float value) {
        uint32_t bits = 0;
        memcpy(&bits, &value, 4);
        u32(bits);
    }
    void text(const char* value) {
        bytes.insert(bytes.end(), value, value + strlen(value) + 1);
    }
};

void writeHeader(Blob& blob, const char* version, uint32_t format, uint32_t submeshes) {
    blob.bytes.insert(blob.bytes.end(), version, version + 8);
    blob.u8(0);
    blob.u32(format);
    blob.u32(1);
    blob.u32(submeshes);
}

// A quad: four position+uv vertices (x offset by `shift`) and two triangles.
void writeQuad(Blob& blob, const char* material, float shift, bool bounded) {
    blob.text(material);
    blob.u32(0);
    if (bounded) {
        for (float value : {-1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f}) blob.f32(value);
        blob.u32(kPositionTexCoord);
    }
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    blob.u32(4 * 5 * sizeof(float));
    for (const auto& corner : corners) {
        blob.f32(corner[0] + shift);
        blob.f32(corner[1]);
        blob.f32(0.0f);
        blob.f32(corner[0] * 0.5f + 0.5f);
        blob.f32(corner[1] * 0.5f + 0.5f);
    }
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    blob.u32(sizeof(indices));
    for (uint16_t index : indices) blob.u16(index);
    if (bounded) {
        for (int i = 0; i < 6; ++i) blob.u8(0);
    }
}

std::vector<uint8_t> makeTwoQuadMesh() {
    Blob blob;
    writeHeader(blob, "MDLV0014", kPositionTexCoord, 2);
    writeQuad(blob, "materials/a.json", 0.0f, false);
    writeQuad(blob, "materials/b.json", 10.0f, false);
    blob.u8(0);
    return blob.bytes;
}

void testTwoSubmeshes() {
    const std::vector<uint8_t> bytes = makeTwoQuadMesh();
    MdlMesh mesh;
    check(parseMdlMesh(bytes.data(), bytes.size(), mesh), "mesh: synthetic MDLV0014 parses");
    check(mesh.version == "MDLV0014", "mesh: version string");
    check(mesh.vertex_stride == 5, "mesh: position + uv stride");
    check(mesh.elements.size() == 2 && mesh.elements[1].attribute == MdlVertexAttribute::TexCoord &&
              mesh.elements[1].offset == 3,
          "mesh: uv element follows the position");
    check(mesh.vertices.size() == 8 * 5, "mesh: eight vertices");
    check(mesh.indices.size() == 12, "mesh: twelve indices");
    check(mesh.submeshes.size() == 2, "mesh: two submeshes");
    if (mesh.submeshes.size() != 2 || mesh.indices.size() != 12) return;

    const MdlSubmesh& second = mesh.submeshes[1];
    check(mesh.submeshes[0].materials.size() == 1 && mesh.submeshes[0].materials[0] == "materials/a.json",
          "mesh: first material path");
    check(second.materials[0] == "materials/b.json", "mesh: second material path");
    check(second.first_vertex == 4 && second.vertex_count == 4, "mesh: second vertex range");
    check(second.first_index == 6 && second.index_count == 6, "mesh: second index range");
    check(mesh.indices[6] == 4 && mesh.indices[11] == 7, "mesh: indices point into the shared vertex buffer");
    check(mesh.vertices[4 * 5] == 9.0f, "mesh: second submesh vertex data");
    check(!second.has_bounds, "mesh: MDLV0014 has no submesh bounds");
}

void testBoundedVersion() {
    Blob blob;
    writeHeader(blob, "MDLV0023", kPositionTexCoord, 1);
    writeQuad(blob, "materials/c.json", 0.0f, true);
    blob.u8(0);
    MdlMesh mesh;
    check(parseMdlMesh(blob.bytes.data(), blob.bytes.size(), mesh), "mesh: synthetic MDLV0023 parses");
    check(mesh.submeshes.size() == 1 && mesh.submeshes[0].has_bounds && mesh.submeshes[0].bounds_max[0] == 1.0f,
          "mesh: MDLV0023 keeps submesh bounds");
}

void testFullVertexFormat() {
    Blob blob;
    writeHeader(blob, "MDLV0014", kFullFormat, 1);
    blob.text("materials/car.json");
    blob.u32(0);
    blob.u32(3 * 12 * sizeof(float));
    for (int v = 0; v < 3; ++v)
        for (int f = 0; f < 12; ++f) blob.f32((float)(v * 100 + f));
    blob.u32(6);
    for (uint16_t index : {0, 1, 2}) blob.u16(index);
    MdlMesh mesh;
    check(parseMdlMesh(blob.bytes.data(), blob.bytes.size(), mesh), "mesh: full vertex format parses");
    check(mesh.vertex_stride == 12 && mesh.elements.size() == 4, "mesh: position, normal, tangent4, uv");
    check(mesh.elements.size() == 4 && mesh.elements[2].attribute == MdlVertexAttribute::Tangent &&
              mesh.elements[2].components == 4 && mesh.elements[3].offset == 10,
          "mesh: tangent carries w and uv sits last");
    check(mesh.vertices.size() == 36 && mesh.vertices[12 + 10] == 110.0f, "mesh: interleaved floats kept in order");
}

void testRejectsBadInput() {
    const std::vector<uint8_t> bytes = makeTwoQuadMesh();
    MdlMesh mesh;
    for (size_t length = 0; length + 1 < bytes.size(); ++length) {
        if (parseMdlMesh(bytes.data(), length, mesh)) {
            check(false, "mesh: truncated data is rejected");
            return;
        }
    }
    check(mesh.vertices.empty() && mesh.submeshes.empty(), "mesh: failed parse leaves the output empty");
    check(!parseMdlMesh(nullptr, 0, mesh), "mesh: null buffer rejected");

    std::vector<uint8_t> bad_magic = bytes;
    bad_magic[0] = 'X';
    check(!parseMdlMesh(bad_magic.data(), bad_magic.size(), mesh), "mesh: bad magic rejected");

    std::vector<uint8_t> skinned = bytes;
    skinned[9 + 3] = 0x01;
    check(!parseMdlMesh(skinned.data(), skinned.size(), mesh), "mesh: skinned format bits rejected");

    std::vector<uint8_t> out_of_range = bytes;
    out_of_range[out_of_range.size() - 3] = 9;
    check(!parseMdlMesh(out_of_range.data(), out_of_range.size(), mesh), "mesh: out-of-range index rejected");
}

}  // namespace

void runMdlMeshTests() {
    testTwoSubmeshes();
    testBoundedVersion();
    testFullVertexFormat();
    testRejectsBadInput();
}
