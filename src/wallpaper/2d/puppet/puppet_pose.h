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
    float time = 0.0f;  // seconds on this layer's own clock (already scaled by rate), advanced while playing
    std::string name;
    bool playing = true;
    bool ended = false;  // a "single" clip reached its last frame; scripts are told once, then it is cleared
    bool once = false;   // play to the last frame and stop whatever the clip's loop mode is (playSingleAnimation)
    bool remove_when_done = false;  // dropped from the model after it ends
    int frames_since_end = 0;       // how long the end has waited to be reported before the layer is dropped anyway
};

// Column-major 4x4 matrix.
struct PuppetMatrix {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

class PuppetPose {
   public:
    void init(const MdlModel& model);
    // Moves the clock of every visible, playing layer by dt * rate; a "single" clip stops (and flags `ended`) at its
    // end.
    void advance(const MdlModel& model, std::vector<PuppetAnimationLayer>& layers, float dt) const;
    void attachmentTransforms(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                              std::unordered_map<std::string, PuppetMatrix>& out) const;
    // Writes skinned xyz positions for every vertex of the model.
    void skin(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers, std::vector<float>& out) const;

    // With root motion off, the root bone ignores the translation its clips animate.
    bool root_motion = true;

    // A script-set local pose replaces the animated one for that bone until cleared.
    void setBoneOverride(size_t bone, const MdlKeyframe& pose);
    void clearBoneOverride(size_t bone);
    // Local pose of every bone as currently animated (overrides included); false when no clip drives the model.
    bool localPose(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                   std::vector<MdlKeyframe>& pose) const;
    // Bone transforms relative to the parent bone (the bind transform when no clip drives the model).
    void localMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                       std::vector<PuppetMatrix>& out) const;
    // Bone transforms in model space, parents applied.
    void worldMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                       std::vector<PuppetMatrix>& out) const;

   private:
    void computeBoneMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers) const;

    struct BoneOverride {
        bool active = false;
        MdlKeyframe pose;
    };
    std::vector<BoneOverride> overrides_;

    std::vector<PuppetMatrix> inverse_bind_world;
    mutable std::vector<PuppetMatrix> skin_matrices;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_PUPPET_POSE_H
