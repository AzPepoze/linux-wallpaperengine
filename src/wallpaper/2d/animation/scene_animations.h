#ifndef SCENE_ANIMATIONS_H
#define SCENE_ANIMATIONS_H

#include <stdint.h>

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "shared/core/engine_context.h"
#include "wallpaper/2d/animation/animation_timelines.h"

// Applies the animated values to the scene tree nodes (origin, scale, angles) and layers (color, alpha), before
// property scripts run.
class SceneAnimations {
   public:
    explicit SceneAnimations(EngineContext& ctx) : ctx_(ctx) {}

    void add(uint32_t object_id, const std::vector<wallpaper_engine::PropertyAnimationDocument>& animations) {
        timelines_.add(object_id, animations);
    }
    void update(float dt);

    uint32_t find(uint32_t object_id, const std::string& key) const {
        return timelines_.find(object_id, key);
    }
    bool get(uint32_t handle, const std::string& field, double& out) const {
        return timelines_.get(handle, field, out);
    }
    bool getString(uint32_t handle, const std::string& field, std::string& out) const {
        return timelines_.getString(handle, field, out);
    }
    bool set(uint32_t handle, const std::string& field, double value) {
        return timelines_.set(handle, field, value);
    }
    bool command(uint32_t handle, const std::string& command) {
        return timelines_.command(handle, command);
    }
    std::vector<uint32_t> takeEnded() {
        return timelines_.takeEnded();
    }
    size_t size() const {
        return timelines_.size();
    }

   private:
    void apply(const AnimatedValue& animated);
    // `effect:<index>:<constant>` animations drive an effect's material constant.
    void applyEffectConstant(Layer* layer, const AnimatedValue& animated);
    Layer* layerFor(uint32_t object_id) const;

    EngineContext& ctx_;
    AnimationTimelines timelines_;
    std::map<std::pair<uint32_t, std::string>, std::array<float, 3>> bases_;
};

#endif  // SCENE_ANIMATIONS_H
