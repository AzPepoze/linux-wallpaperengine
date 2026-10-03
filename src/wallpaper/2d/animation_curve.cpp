#include "animation_curve.h"

#include <algorithm>
#include <cmath>

float evaluateCurve(const std::vector<wallpaper_engine::CurveKeyframe>& keys, float fps, float length,
                    const std::string& mode, float time_seconds) {
    if (keys.empty()) return 0.0f;

    const float effective_length = length > 0.0f ? length : keys.back().frame;
    float frame = std::max(0.0f, time_seconds * std::max(0.0f, fps));
    if (effective_length > 0.0f) {
        if (mode == "single") {
            frame = std::min(frame, effective_length);
        } else if (mode == "mirror") {
            const float period = effective_length * 2.0f;
            frame = std::fmod(frame, period);
            if (frame > effective_length) frame = period - frame;
        } else {
            frame = std::fmod(frame, effective_length);
        }
    }

    if (frame <= keys.front().frame) return keys.front().value;
    for (size_t index = 1; index < keys.size(); ++index) {
        const auto& right = keys[index];
        const auto& left = keys[index - 1];
        if (frame <= right.frame) {
            const float span = right.frame - left.frame;
            const float amount = span > 0.0f ? (frame - left.frame) / span : 0.0f;
            return left.value + (right.value - left.value) * amount;
        }
    }
    return keys.back().value;
}
