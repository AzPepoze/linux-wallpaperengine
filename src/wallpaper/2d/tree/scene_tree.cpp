#include "scene_tree.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "linmath.h"

namespace {

void localTransform(const SceneTreeNode& node, mat4x4 out) {
    // Wallpaper Engine scene-node local transform order: T * Rz * Ry * Rx * S.
    constexpr float kDegToRad = 0.01745329251994329577f;
    mat4x4_identity(out);
    mat4x4_translate_in_place(out, node.origin[0], node.origin[1], node.origin[2]);
    mat4x4_rotate_Z(out, out, node.angles[2] * kDegToRad);
    mat4x4_rotate_Y(out, out, node.angles[1] * kDegToRad);
    mat4x4_rotate_X(out, out, node.angles[0] * kDegToRad);
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

void SceneTree::removeNode(uint32_t id) {
    if (nodes_.erase(id) == 0) return;
    for (auto& [node_id, node] : nodes_) {
        (void)node_id;
        if (node.parent_id == id) node.parent_id = 0;
    }
    rebuildHierarchy();
}

void SceneTree::rotationFromAngles(const float angles[3], mat4x4 out) {
    constexpr float kDegToRad = 0.01745329251994329577f;
    mat4x4_identity(out);
    mat4x4_rotate_Z(out, out, angles[2] * kDegToRad);
    mat4x4_rotate_Y(out, out, angles[1] * kDegToRad);
    mat4x4_rotate_X(out, out, angles[0] * kDegToRad);
}

void SceneTree::anglesFromRotation(const mat4x4 m, float angles[3]) {
    constexpr float kRadToDeg = 57.29577951308232f;
    const float sin_y = std::clamp(-m[0][2], -1.0f, 1.0f);
    angles[1] = std::asin(sin_y) * kRadToDeg;
    if (std::fabs(sin_y) < 0.99999f) {
        angles[0] = std::atan2(m[1][2], m[2][2]) * kRadToDeg;
        angles[2] = std::atan2(m[0][1], m[0][0]) * kRadToDeg;
    } else {
        // Looking straight up or down: x and z are one rotation, so it all goes on z.
        angles[0] = 0.0f;
        angles[2] = std::atan2(-m[1][0], m[1][1]) * kRadToDeg;
    }
}

void SceneTree::decompose(const mat4x4 m, SceneTreeNode& node) {
    mat4x4 rotation;
    mat4x4_identity(rotation);
    for (int column = 0; column < 3; ++column) {
        const float length =
            std::sqrt(m[column][0] * m[column][0] + m[column][1] * m[column][1] + m[column][2] * m[column][2]);
        node.scale[(size_t)column] = length;
        for (int row = 0; row < 3; ++row) rotation[column][row] = length > 1e-8f ? m[column][row] / length : 0.0f;
    }
    for (int row = 0; row < 3; ++row) node.origin[(size_t)row] = m[3][row];
    float angles[3];
    anglesFromRotation(rotation, angles);
    for (size_t i = 0; i < 3; ++i) node.angles[i] = angles[i];
}

bool SceneTree::ancestorsVisible(uint32_t id) const {
    const SceneTreeNode* node = find(id);
    // The step bound keeps a malformed parent cycle from looping.
    for (size_t steps = 0; node && node->parent_id != 0 && steps < nodes_.size(); ++steps) {
        node = find(node->parent_id);
        if (node && !node->visible) return false;
    }
    return true;
}

uint32_t SceneTree::maxId() const {
    uint32_t highest = 0;
    for (const auto& [id, node] : nodes_) {
        (void)node;
        highest = std::max(highest, id);
    }
    return highest;
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

SceneTreeNode* SceneTree::find(uint32_t id) {
    if (id == 0) return nullptr;
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

std::vector<uint32_t> SceneTree::rootIds() const {
    std::vector<uint32_t> roots;
    roots.reserve(nodes_.size());
    for (const auto& [id, node] : nodes_) {
        if (node.parent_id == 0 || nodes_.find(node.parent_id) == nodes_.end()) roots.push_back(id);
    }
    std::sort(roots.begin(), roots.end());
    return roots;
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
    const SceneTreeNode* parent = nullptr;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (parent && !(*it)->attachment.empty()) {
            const auto attachment = parent->attachment_transforms.find((*it)->attachment);
            if (attachment != parent->attachment_transforms.end()) {
                mat4x4 attachment_matrix;
                memcpy(attachment_matrix, attachment->second.data(), sizeof(mat4x4));
                mat4x4_mul(world, world, attachment_matrix);
            }
        }
        mat4x4 local;
        ::localTransform(**it, local);
        mat4x4_mul(world, world, local);
        parent = *it;
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
