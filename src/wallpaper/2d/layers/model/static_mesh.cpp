#include "static_mesh.h"

#include <algorithm>

namespace {

using wallpaper_engine::MdlVertexAttribute;
using wallpaper_engine::MdlVertexElement;

struct Destination {
    float* values;
    uint32_t capacity;
};

Destination destinationFor(MdlVertexAttribute attribute, mesh_vertex_t& vertex) {
    switch (attribute) {
        case MdlVertexAttribute::Position:
            return {vertex.position, 3};
        case MdlVertexAttribute::Normal:
            return {vertex.normal, 3};
        case MdlVertexAttribute::Tangent:
            return {vertex.tangent, 4};
        case MdlVertexAttribute::TexCoord:
            return {vertex.uv, 2};
    }
    return {nullptr, 0};
}

void copyElement(const MdlVertexElement& element, const float* source, mesh_vertex_t& vertex) {
    const Destination destination = destinationFor(element.attribute, vertex);
    const uint32_t count = std::min(element.components, destination.capacity);
    std::copy(source + element.offset, source + element.offset + count, destination.values);
}

}  // namespace

std::vector<mesh_vertex_t> packMeshVertices(const wallpaper_engine::MdlMesh& mesh) {
    if (mesh.vertex_stride == 0) return {};
    const size_t count = mesh.vertices.size() / mesh.vertex_stride;
    std::vector<mesh_vertex_t> packed(count, mesh_vertex_t{});
    for (size_t index = 0; index < count; ++index) {
        const float* source = &mesh.vertices[index * mesh.vertex_stride];
        for (const MdlVertexElement& element : mesh.elements) copyElement(element, source, packed[index]);
    }
    return packed;
}

bool submeshDrawable(const wallpaper_engine::MdlSubmesh& submesh, size_t index_total) {
    const size_t end = (size_t)submesh.first_index + submesh.index_count;
    return submesh.index_count >= 3 && submesh.index_count % 3 == 0 && end <= index_total;
}

void orderMeshDraws(std::vector<MeshDraw>& draws, const std::vector<bool>& translucent_materials) {
    const auto opaque = [&](const MeshDraw& draw) {
        return draw.material >= translucent_materials.size() || !translucent_materials[draw.material];
    };
    std::stable_partition(draws.begin(), draws.end(), opaque);
}
