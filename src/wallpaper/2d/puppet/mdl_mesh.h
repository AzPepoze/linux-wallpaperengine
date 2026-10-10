#ifndef WALLPAPER_ENGINE_MDL_MESH_H
#define WALLPAPER_ENGINE_MDL_MESH_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace wallpaper_engine {

// Static (unskinned) 3D .mdl mesh, as used by scene `model` objects.

enum class MdlVertexAttribute {
    Position,
    Normal,
    Tangent,  // xyz plus handedness in w
    TexCoord,
};

struct MdlVertexElement {
    MdlVertexAttribute attribute = MdlVertexAttribute::Position;
    uint32_t components = 0;
    uint32_t offset = 0;  // in floats from the start of the vertex
};

struct MdlSubmesh {
    // The first entry is the default material; any others are alternate skins.
    std::vector<std::string> materials;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    bool has_bounds = false;
    float bounds_min[3] = {0, 0, 0};
    float bounds_max[3] = {0, 0, 0};
};

struct MdlMesh {
    std::string version;
    uint32_t vertex_format = 0;  // raw attribute bit mask from the header
    uint32_t vertex_stride = 0;  // floats per vertex
    std::vector<MdlVertexElement> elements;
    std::vector<float> vertices;
    // Triangle list; indices already point into the shared vertex buffer.
    std::vector<uint32_t> indices;
    std::vector<MdlSubmesh> submeshes;
};

// False for skinned/puppet models, unknown attribute bits, or malformed and truncated data.
bool parseMdlMesh(const uint8_t* data, size_t size, MdlMesh& out);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MDL_MESH_H
