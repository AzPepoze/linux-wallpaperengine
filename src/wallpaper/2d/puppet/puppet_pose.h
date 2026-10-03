#ifndef WALLPAPER_ENGINE_PUPPET_POSE_H
#define WALLPAPER_ENGINE_PUPPET_POSE_H

#include <string>
#include <unordered_map>
#include <vector>

#include "wallpaper/2d/puppet/mdl_parser.h"

namespace wallpaper_engine {

struct PuppetAnimationLayer {
    uint32_t animation_id = 0;
    float rate = 1.0f;
    float blend = 1.0f;
    bool additive = false;
    bool visible = true;
    float time = 0.0f;
};

// Column-major 4x4 matrix.
struct PuppetMatrix {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

class PuppetPose {
   public:
    void init(const MdlModel& model);
    void advance(std::vector<PuppetAnimationLayer>& layers, float dt) const;
    void attachmentTransforms(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                              std::unordered_map<std::string, PuppetMatrix>& out) const;
    // Writes skinned xyz positions for every vertex of the model.
    void skin(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers, std::vector<float>& out) const;

   private:
    void computeBoneMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers) const;

    std::vector<PuppetMatrix> inverse_bind_world;
    mutable std::vector<PuppetMatrix> skin_matrices;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_PUPPET_POSE_H
