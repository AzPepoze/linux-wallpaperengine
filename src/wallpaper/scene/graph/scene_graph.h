#ifndef SCENE_GRAPH_H
#define SCENE_GRAPH_H

#include <stdint.h>

#include <array>
#include <unordered_map>
#include <vector>

struct SceneGraphNode {
    uint32_t id = 0;
    uint32_t parent_id = 0;
    std::array<float, 3> origin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> angles = {0.0f, 0.0f, 0.0f};
    std::array<float, 2> parallax_depth = {0.0f, 0.0f};
    bool propagate_to_children = true;
    std::vector<uint32_t> children;
};

class SceneGraph {
   public:
    void clear();
    void addNode(const SceneGraphNode& node);
    void rebuildHierarchy();

    const SceneGraphNode* find(uint32_t id) const;
    const SceneGraphNode* resolveParallaxNode(uint32_t id) const;
    bool worldPosition(uint32_t id, float out[3]) const;

    size_t size() const {
        return nodes_.size();
    }

   private:
    std::unordered_map<uint32_t, SceneGraphNode> nodes_;
};

#endif  // SCENE_GRAPH_H
