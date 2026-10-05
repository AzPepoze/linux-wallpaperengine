#ifndef SCENE_VISIBILITY_H
#define SCENE_VISIBILITY_H

#include <unordered_map>

#include "shared/core/engine_context.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

// Drawable nodes keep their live flag on the layer; groups keep it on the tree.
// Resolve both without copying inherited visibility into a child's own flag.
class SceneVisibility {
   public:
    explicit SceneVisibility(const EngineContext& ctx) : tree_(ctx.scene.scene_tree) {
        for (const Layer* layer : ctx.scene.layers)
            if (layer->scene_object_id) layers_.emplace(layer->scene_object_id, layer);
    }

    bool visible(const Layer& layer) const {
        return layer.visible &&
               (!tree_ || tree_->ancestorsVisible(layer.scene_object_id, [&](const SceneTreeNode& node) {
                   const auto found = layers_.find(node.id);
                   return found == layers_.end() ? node.visible : found->second->visible;
               }));
    }

   private:
    const SceneTree* tree_;
    std::unordered_map<uint32_t, const Layer*> layers_;
};

#endif
