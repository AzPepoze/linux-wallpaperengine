#ifndef SCENE_TREE_H
#define SCENE_TREE_H

#include <stdint.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "linmath.h"

struct SceneTreeNode {
    uint32_t id = 0;
    uint32_t parent_id = 0;
    std::string name;
    std::string attachment;
    std::unordered_map<std::string, std::array<float, 16>> attachment_transforms;
    std::array<float, 3> origin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> angles = {0.0f, 0.0f, 0.0f};
    std::array<float, 2> parallax_depth = {0.0f, 0.0f};
    bool propagate_to_children = true;
    bool visible = true;  // groups only; layers keep their own flag
    std::vector<uint32_t> children;
};

class SceneTree {
   public:
    void clear();
    void addNode(const SceneTreeNode& node);
    void rebuildHierarchy();
    // Children of a removed node become roots.
    void removeNode(uint32_t id);
    uint32_t maxId() const;

    // Rotation of the tree's node order (Rz * Ry * Rx) for angles in degrees, and its inverse for a pure rotation.
    static void rotationFromAngles(const float angles[3], mat4x4 out);
    static void anglesFromRotation(const mat4x4 rotation, float angles[3]);
    // Splits an affine matrix into the node's origin, scale and angles.
    static void decompose(const mat4x4 matrix, SceneTreeNode& node);
    // False when a group above the node is hidden.
    bool ancestorsVisible(uint32_t id) const;

    const SceneTreeNode* find(uint32_t id) const;
    SceneTreeNode* find(uint32_t id);
    std::vector<uint32_t> rootIds() const;
    const SceneTreeNode* resolveParallaxNode(uint32_t id) const;
    bool localTransform(uint32_t id, mat4x4 out) const;
    bool worldTransform(uint32_t id, mat4x4 out) const;
    bool worldPosition(uint32_t id, float out[3]) const;

    size_t size() const {
        return nodes_.size();
    }

   private:
    std::unordered_map<uint32_t, SceneTreeNode> nodes_;
};

#endif  // SCENE_TREE_H
