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
    std::array<float, 3> origin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> angles = {0.0f, 0.0f, 0.0f};
    std::array<float, 2> parallax_depth = {0.0f, 0.0f};
    bool propagate_to_children = true;
    std::vector<uint32_t> children;
};

class SceneTree {
   public:
    void clear();
    void addNode(const SceneTreeNode& node);
    void rebuildHierarchy();

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
