#include "scene_tree.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "../../../../libs/linmath.h"

namespace {

void localTransform(const SceneTreeNode& node, mat4x4 out) {
    // Wallpaper Engine scene-node local transform order: T * Rz * Ry * Rx * S.
    mat4x4_identity(out);
    mat4x4_translate_in_place(out, node.origin[0], node.origin[1], node.origin[2]);
    mat4x4_rotate_Z(out, out, node.angles[2]);
    mat4x4_rotate_Y(out, out, node.angles[1]);
    mat4x4_rotate_X(out, out, node.angles[0]);
    mat4x4_scale_aniso(out, out, node.scale[0], node.scale[1], node.scale[2]);
}

}  // namespace

void SceneTree::clear() {
    nodes_.clear();
}

void SceneTree::addNode(const SceneTreeNode& node) {
    if (node.id == 0) return;
    nodes_[node.id] = node;
}

void SceneTree::rebuildHierarchy() {
    for (auto& [id, node] : nodes_) {
        (void)id;
        node.children.clear();
    }

    for (const auto& [id, node] : nodes_) {
        if (node.parent_id == 0) continue;
        auto parent = nodes_.find(node.parent_id);
        if (parent != nodes_.end()) parent->second.children.push_back(id);
    }

    for (auto& [id, node] : nodes_) {
        (void)id;
        std::sort(node.children.begin(), node.children.end());
    }
}

const SceneTreeNode* SceneTree::find(uint32_t id) const {
    if (id == 0) return nullptr;
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const SceneTreeNode* SceneTree::resolveParallaxNode(uint32_t id) const {
    const SceneTreeNode* node = find(id);
    if (!node) return nullptr;

    const SceneTreeNode* resolved = node;
    uint32_t parent_id = node->parent_id;
    std::unordered_set<uint32_t> visited;
    visited.insert(node->id);

    while (parent_id != 0 && visited.insert(parent_id).second) {
        const SceneTreeNode* candidate = find(parent_id);
        if (!candidate || !candidate->propagate_to_children) break;
        resolved = candidate;
        parent_id = candidate->parent_id;
    }

    return resolved;
}

bool SceneTree::localTransform(uint32_t id, mat4x4 out) const {
    const SceneTreeNode* node = find(id);
    if (!node || !out) return false;
    ::localTransform(*node, out);
    return true;
}

bool SceneTree::worldTransform(uint32_t id, mat4x4 out) const {
    const SceneTreeNode* node = find(id);
    if (!node || !out) return false;

    std::vector<const SceneTreeNode*> chain;
    chain.reserve(8);
    std::unordered_set<uint32_t> visited;

    const SceneTreeNode* current = node;
    while (current && visited.insert(current->id).second) {
        chain.push_back(current);
        if (current->parent_id == 0) break;
        current = find(current->parent_id);
    }

    if (chain.empty()) return false;

    mat4x4 world;
    mat4x4_identity(world);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        mat4x4 local;
        ::localTransform(**it, local);
        mat4x4_mul(world, world, local);
    }

    memcpy(out, world, sizeof(mat4x4));
    return true;
}

bool SceneTree::worldPosition(uint32_t id, float out[3]) const {
    mat4x4 world;
    if (!worldTransform(id, world)) return false;

    out[0] = world[3][0];
    out[1] = world[3][1];
    out[2] = world[3][2];
    return true;
}
