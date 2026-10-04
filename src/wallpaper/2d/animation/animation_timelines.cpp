#include "animation_timelines.h"

#include <algorithm>

void AnimationTimelines::add(uint32_t object_id,
                             const std::vector<wallpaper_engine::PropertyAnimationDocument>& animations) {
    std::vector<bool> assigned(animations.size(), false);

    auto addChannel = [](Timeline& timeline, const wallpaper_engine::PropertyAnimationDocument& source) {
        Channel channel;
        channel.property = source.property;
        for (size_t i = 0; i < 3; ++i) channel.curves[i] = source.curves[i];
        channel.relative = source.relative;
        timeline.channels.push_back(std::move(channel));
    };
    auto startTimeline = [&](const wallpaper_engine::PropertyAnimationDocument& root) {
        Timeline timeline;
        timeline.id = ++next_handle_;
        timeline.object_id = object_id;
        timeline.name = root.name;
        timeline.playing = !root.start_paused;
        for (const wallpaper_engine::AnimationCurve& curve : root.curves) {
            if (curve.keys.empty()) continue;
            timeline.fps = curve.fps > 0.0f ? curve.fps : 30.0;
            timeline.length_frames = curve.length > 0.0f ? curve.length : curve.keys.back().frame;
            timeline.single = curve.mode == "single";
            break;
        }
        return timeline;
    };

    // A timeline root has no parent; the animations that name it as parent (or that it lists as children) join it.
    for (size_t i = 0; i < animations.size(); ++i) {
        const auto& root = animations[i];
        if (!root.parent.empty()) continue;
        Timeline timeline = startTimeline(root);
        addChannel(timeline, root);
        assigned[i] = true;
        for (size_t j = 0; j < animations.size(); ++j) {
            if (assigned[j]) continue;
            const bool listed =
                std::find(root.children.begin(), root.children.end(), animations[j].property) != root.children.end();
            if (!listed && animations[j].parent != root.property) continue;
            addChannel(timeline, animations[j]);
            assigned[j] = true;
        }
        timelines_.push_back(std::move(timeline));
    }
    // Children whose root is missing still animate, each on its own clock.
    for (size_t i = 0; i < animations.size(); ++i) {
        if (assigned[i]) continue;
        Timeline timeline = startTimeline(animations[i]);
        addChannel(timeline, animations[i]);
        timelines_.push_back(std::move(timeline));
    }
}

double AnimationTimelines::lengthSeconds(const Timeline& timeline) {
    return timeline.fps > 0.0 ? timeline.length_frames / timeline.fps : 0.0;
}

void AnimationTimelines::update(float dt, const std::function<void(const AnimatedValue&)>& apply) {
    for (Timeline& timeline : timelines_) {
        if (timeline.playing) {
            timeline.time = std::max(0.0, timeline.time + (double)dt * timeline.rate);
            const double length = lengthSeconds(timeline);
            if (timeline.single && length > 0.0 && timeline.time >= length) {
                timeline.time = length;
                timeline.playing = false;
                ended_.push_back(timeline.id);
            }
        }
        for (const Channel& channel : timeline.channels) {
            AnimatedValue animated;
            animated.object_id = timeline.object_id;
            animated.property = channel.property;
            animated.relative = channel.relative;
            for (size_t i = 0; i < 3; ++i) {
                const wallpaper_engine::AnimationCurve& curve = channel.curves[i];
                if (curve.keys.empty()) continue;
                animated.value[i] =
                    evaluateCurve(curve.keys, curve.fps, curve.length, curve.mode, (float)timeline.time);
                animated.has[i] = true;
            }
            apply(animated);
        }
    }
}

AnimationTimelines::Timeline* AnimationTimelines::byHandle(uint32_t handle) {
    for (Timeline& timeline : timelines_)
        if (timeline.id == handle) return &timeline;
    return nullptr;
}

const AnimationTimelines::Timeline* AnimationTimelines::byHandle(uint32_t handle) const {
    for (const Timeline& timeline : timelines_)
        if (timeline.id == handle) return &timeline;
    return nullptr;
}

uint32_t AnimationTimelines::find(uint32_t object_id, const std::string& key) const {
    for (const Timeline& timeline : timelines_) {
        if (timeline.object_id != object_id) continue;
        if (key.empty() || timeline.name == key) return timeline.id;
        for (const Channel& channel : timeline.channels)
            if (channel.property == key) return timeline.id;
    }
    return 0;
}

bool AnimationTimelines::get(uint32_t handle, const std::string& field, double& out) const {
    const Timeline* timeline = byHandle(handle);
    if (!timeline) return false;
    if (field == "rate") {
        out = timeline->rate;
    } else if (field == "fps") {
        out = timeline->fps;
    } else if (field == "frameCount") {
        out = timeline->length_frames;
    } else if (field == "duration") {
        out = lengthSeconds(*timeline);
    } else if (field == "frame") {
        out = timeline->time * timeline->fps;
    } else if (field == "playing") {
        out = timeline->playing ? 1.0 : 0.0;
    } else {
        return false;
    }
    return true;
}

bool AnimationTimelines::getString(uint32_t handle, const std::string& field, std::string& out) const {
    const Timeline* timeline = byHandle(handle);
    if (!timeline || field != "name") return false;
    out = timeline->name;
    return true;
}

bool AnimationTimelines::set(uint32_t handle, const std::string& field, double value) {
    Timeline* timeline = byHandle(handle);
    if (!timeline) return false;
    if (field == "rate") {
        timeline->rate = value;
    } else if (field == "frame") {
        timeline->time = timeline->fps > 0.0 ? std::max(0.0, value / timeline->fps) : 0.0;
    } else {
        return false;
    }
    return true;
}

bool AnimationTimelines::command(uint32_t handle, const std::string& command) {
    Timeline* timeline = byHandle(handle);
    if (!timeline) return false;
    if (command == "play") {
        const double length = lengthSeconds(*timeline);
        if (timeline->single && length > 0.0 && timeline->time >= length) timeline->time = 0.0;
        timeline->playing = true;
    } else if (command == "pause") {
        timeline->playing = false;
    } else if (command == "stop") {
        timeline->playing = false;
        timeline->time = 0.0;
    } else {
        return false;
    }
    return true;
}

std::vector<uint32_t> AnimationTimelines::takeEnded() {
    std::vector<uint32_t> ended;
    ended.swap(ended_);
    return ended;
}
