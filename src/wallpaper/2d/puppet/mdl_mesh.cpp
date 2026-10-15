#include "mdl_mesh.h"

#include <ctype.h>
#include <string.h>

#include "mdl_reader.h"

namespace wallpaper_engine {
namespace {

using mdl_detail::Reader;

constexpr uint32_t kPositionBit = 0x01;
constexpr uint32_t kNormalBit = 0x02;
constexpr uint32_t kTangentBit = 0x04;
constexpr uint32_t kTexCoordBit = 0x08;
// Seen alone in place of kTexCoordBit; packs four texcoord floats per vertex.
constexpr uint32_t kTexCoord4Bit = 0x20;
constexpr uint32_t kKnownFormatBits = kPositionBit | kNormalBit | kTangentBit | kTexCoordBit | kTexCoord4Bit;

// From this version on, each submesh carries its bounds and its own vertex format.
constexpr int kBoundedSubmeshVersion = 23;
constexpr size_t kBoundedSubmeshTrailer = 6;

constexpr uint32_t kMaxSkins = 64;
constexpr uint32_t kMaxSubmeshes = 4096;

struct MeshHeader {
    int version = 0;
    uint32_t format = 0;
    uint32_t skin_count = 0;
    uint32_t submesh_count = 0;
};

bool parseVersion(const uint8_t* data, size_t size, int& out) {
    if (size < 8 || memcmp(data, "MDLV", 4) != 0) return false;
    int version = 0;
    for (size_t i = 4; i < 8; ++i) {
        if (!isdigit(data[i])) return false;
        version = version * 10 + (data[i] - '0');
    }
    out = version;
    return true;
}

void addElement(MdlMesh& mesh, MdlVertexAttribute attribute, uint32_t components) {
    mesh.elements.push_back({attribute, components, mesh.vertex_stride});
    mesh.vertex_stride += components;
}

bool buildVertexLayout(uint32_t format, MdlMesh& mesh) {
    const bool known = (format & ~kKnownFormatBits) == 0;
    const bool has_position = (format & kPositionBit) != 0;
    const bool both_texcoords = (format & kTexCoordBit) && (format & kTexCoord4Bit);
    if (!known || !has_position || both_texcoords) return false;

    mesh.vertex_format = format;
    addElement(mesh, MdlVertexAttribute::Position, 3);
    if (format & kNormalBit) addElement(mesh, MdlVertexAttribute::Normal, 3);
    if (format & kTangentBit) addElement(mesh, MdlVertexAttribute::Tangent, 4);
    if (format & kTexCoordBit) addElement(mesh, MdlVertexAttribute::TexCoord, 2);
    if (format & kTexCoord4Bit) addElement(mesh, MdlVertexAttribute::TexCoord, 4);
    return true;
}

bool readHeader(Reader& r, MeshHeader& header) {
    if (!parseVersion(r.data, r.size, header.version) || !r.skip(8)) return false;
    uint8_t version_terminator = 0;
    if (!r.u8(version_terminator) || !r.u32(header.format) || !r.u32(header.skin_count) ||
        !r.u32(header.submesh_count)) {
        return false;
    }
    if (header.skin_count == 0 || header.skin_count > kMaxSkins) return false;
    return header.submesh_count > 0 && header.submesh_count <= kMaxSubmeshes;
}

bool readBounds(Reader& r, uint32_t format, MdlSubmesh& submesh) {
    for (float& value : submesh.bounds_min)
        if (!r.f32(value)) return false;
    for (float& value : submesh.bounds_max)
        if (!r.f32(value)) return false;
    uint32_t submesh_format = 0;
    if (!r.u32(submesh_format) || submesh_format != format) return false;
    submesh.has_bounds = true;
    return true;
}

bool readVertices(Reader& r, MdlMesh& mesh, MdlSubmesh& submesh) {
    uint32_t byte_count = 0;
    const size_t stride_bytes = (size_t)mesh.vertex_stride * sizeof(float);
    if (!r.u32(byte_count) || byte_count == 0 || byte_count % stride_bytes != 0 || !r.has(byte_count)) return false;

    submesh.first_vertex = (uint32_t)(mesh.vertices.size() / mesh.vertex_stride);
    submesh.vertex_count = (uint32_t)(byte_count / stride_bytes);
    const size_t first_float = mesh.vertices.size();
    mesh.vertices.resize(first_float + byte_count / sizeof(float));
    memcpy(mesh.vertices.data() + first_float, r.data + r.pos, byte_count);
    return r.skip(byte_count);
}

bool readIndices(Reader& r, MdlMesh& mesh, MdlSubmesh& submesh) {
    uint32_t byte_count = 0;
    constexpr size_t kTriangleBytes = 3 * sizeof(uint16_t);
    if (!r.u32(byte_count) || byte_count == 0 || byte_count % kTriangleBytes != 0 || !r.has(byte_count)) return false;

    submesh.first_index = (uint32_t)mesh.indices.size();
    submesh.index_count = byte_count / sizeof(uint16_t);
    mesh.indices.reserve(mesh.indices.size() + submesh.index_count);
    for (uint32_t i = 0; i < submesh.index_count; ++i) {
        uint16_t index = 0;
        if (!r.u16(index) || index >= submesh.vertex_count) return false;
        mesh.indices.push_back(submesh.first_vertex + index);
    }
    return true;
}

bool readSubmesh(Reader& r, const MeshHeader& header, MdlMesh& mesh) {
    MdlSubmesh submesh;
    submesh.materials.resize(header.skin_count);
    for (std::string& material : submesh.materials)
        if (!r.cstring(material)) return false;

    uint32_t reserved = 0;
    if (!r.u32(reserved)) return false;
    const bool bounded = header.version >= kBoundedSubmeshVersion;
    if (bounded && !readBounds(r, header.format, submesh)) return false;
    if (!readVertices(r, mesh, submesh) || !readIndices(r, mesh, submesh)) return false;
    if (bounded && !r.skip(kBoundedSubmeshTrailer)) return false;

    mesh.submeshes.push_back(std::move(submesh));
    return true;
}

}  // namespace

bool parseMdlMesh(const uint8_t* data, size_t size, MdlMesh& out) {
    out = MdlMesh{};
    if (!data) return false;

    Reader r{data, size, 0};
    MeshHeader header;
    MdlMesh mesh;
    if (!readHeader(r, header) || !buildVertexLayout(header.format, mesh)) return false;
    mesh.version.assign((const char*)data, 8);

    for (uint32_t i = 0; i < header.submesh_count; ++i)
        if (!readSubmesh(r, header, mesh)) return false;

    out = std::move(mesh);
    return true;
}

}  // namespace wallpaper_engine
