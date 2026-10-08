#ifndef ANIMATION_TIMELINES_H
#define ANIMATION_TIMELINES_H

#include <stdint.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "wallpaper/2d/animation_curve.h"
#include "wallpaper/2d/parser/scene_document.h"

// Components without keyframes have has[i] false.
struct AnimatedValue {
    uint32_t object_id = 0;
    std::string property;  // "origin", "scale", "angles" (radians), "color", "alpha"
    double value[3] = {0.0, 0.0, 0.0};
    bool has[3] = {false, false, false};
    bool relative = false;  // the value is an offset from the property's value before animation
};

// Parent/child-linked properties share one timeline clock, so a script can pause the group.
class AnimationTimelines {
   public:
    void add(uint32_t object_id, const std::vector<wallpaper_engine::PropertyAnimationDocument>& animations);

    void update(float dt, const std::function<void(const AnimatedValue&)>& apply);

    // `key` is a timeline name or an animated property name; empty picks the object's first timeline. 0 = none.
    uint32_t find(uint32_t object_id, const std::string& key) const;
    bool get(uint32_t handle, const std::string& field, double& out) const;
    bool getString(uint32_t handle, const std::string& field, std::string& out) const;
    bool set(uint32_t handle, const std::string& field, double value);  // rate, frame
    bool command(uint32_t handle, const std::string& command);          // play, stop, pause
    std::vector<uint32_t> takeEnded();                                  // single-mode timelines that finished

    size_t size() const {
        return timelines_.size();
    }

   private:
    struct Channel {
        std::string property;
        std::array<wallpaper_engine::AnimationCurve, 3> curves;
        bool relative = false;
    };
    struct Timeline {
        uint32_t id = 0;
        uint32_t object_id = 0;
        std::string name;
        std::vector<Channel> channels;
        double fps = 30.0;
        double length_frames = 0.0;
        bool single = false;  // plays once and holds its last frame
        double time = 0.0;    // seconds on this timeline's own clock
        double rate = 1.0;
        bool playing = true;
    };

    Timeline* byHandle(uint32_t handle);
    const Timeline* byHandle(uint32_t handle) const;
    static double lengthSeconds(const Timeline& timeline);

    std::vector<Timeline> timelines_;
    std::vector<uint32_t> ended_;
    uint32_t next_handle_ = 0;
};

#endif  // ANIMATION_TIMELINES_H
