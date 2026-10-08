#ifndef MODEL_DATA_H
#define MODEL_DATA_H

#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

// Script geometry: vertex attributes form one interleaved float buffer in `format` order.
enum class VertexAttribute : uint8_t { Position, Normal, Uv, TangentSigned, Color };

inline int vertexAttributeFloats(VertexAttribute attribute) {
    switch (attribute) {
        case VertexAttribute::Position:
        case VertexAttribute::Normal:
            return 3;
        case VertexAttribute::Uv:
            return 2;
        case VertexAttribute::TangentSigned:
        case VertexAttribute::Color:
            return 4;
    }
    return 0;
}

struct ModelShape {
    std::vector<float> vertices;
    std::vector<VertexAttribute> format;
    std::vector<uint32_t> indices;  // empty: the vertices are consecutive triangles
    std::string material;
    float origin[3] = {0.0f, 0.0f, 0.0f};

    int stride() const {
        int floats = 0;
        for (VertexAttribute attribute : format) floats += vertexAttributeFloats(attribute);
        return floats;
    }
    // Offset of an attribute inside one vertex, or -1.
    int offsetOf(VertexAttribute wanted) const {
        int offset = 0;
        for (VertexAttribute attribute : format) {
            if (attribute == wanted) return offset;
            offset += vertexAttributeFloats(attribute);
        }
        return -1;
    }
};

// What a script passes for one shape; only the fields marked present change an existing shape.
struct ShapePatch {
    int index = -1;
    bool remove = false;  // null in replaceData
    bool has_vertices = false;
    bool has_format = false;
    bool has_indices = false;
    bool remove_indices = false;
    bool has_material = false;
    bool has_origin = false;
    ModelShape data;
};

struct ModelData {
    std::vector<ModelShape> shapes;
    uint32_t revision = 1;  // bumped on every change so layers know to upload again
};

// Applies one patch to a shape. Without `replace`, only the fields the patch carries change.
inline void applyShapePatch(ModelShape& shape, const ShapePatch& patch, bool replace) {
    if (replace) shape = ModelShape();
    if (patch.has_vertices) shape.vertices = patch.data.vertices;
    if (patch.has_format) shape.format = patch.data.format;
    if (patch.has_indices) shape.indices = patch.data.indices;
    if (patch.remove_indices) shape.indices.clear();
    if (patch.has_material) shape.material = patch.data.material;
    if (patch.has_origin) std::copy(patch.data.origin, patch.data.origin + 3, shape.origin);
}

#endif  // MODEL_DATA_H
