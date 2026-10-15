#ifndef STATIC_MESH_H
#define STATIC_MESH_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "shared/graphics/render.h"
#include "wallpaper/2d/puppet/mdl_mesh.h"

// One indexed draw of a static model: a submesh and the material it uses.
struct MeshDraw {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    size_t material = 0;
};

// Repacks the mesh into mesh_vertex_t; attributes the mesh lacks stay zero.
std::vector<mesh_vertex_t> packMeshVertices(const wallpaper_engine::MdlMesh& mesh);

// False when a submesh's index range runs past the index buffer or holds no whole triangle.
bool submeshDrawable(const wallpaper_engine::MdlSubmesh& submesh, size_t index_total);

// Opaque draws first and translucent ones after, each group in its original order.
void orderMeshDraws(std::vector<MeshDraw>& draws, const std::vector<bool>& translucent_materials);

#endif  // STATIC_MESH_H
