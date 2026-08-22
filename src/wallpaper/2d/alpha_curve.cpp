#include "alpha_curve.h"

#include <algorithm>
#include <cmath>

float evaluateImageAlpha(const wallpaper_engine::ImageObjectDocument& image, float time_seconds) {
    if (image.alpha_keys.empty()) return image.alpha;

    const float length = image.alpha_length > 0.0f ? image.alpha_length : image.alpha_keys.back().frame;
    float frame = std::max(0.0f, time_seconds * std::max(0.0f, image.alpha_fps));
    if (length > 0.0f) {
        if (image.alpha_mode == "mirror") {
            const float period = length * 2.0f;
            frame = std::fmod(frame, period);
            if (frame > length) frame = period - frame;
        } else {
            frame = std::fmod(frame, length);
        }
    }

    if (frame <= image.alpha_keys.front().frame) return image.alpha_keys.front().value;
    for (size_t index = 1; index < image.alpha_keys.size(); ++index) {
        const auto& right = image.alpha_keys[index];
        const auto& left = image.alpha_keys[index - 1];
        if (frame <= right.frame) {
            const float span = right.frame - left.frame;
            const float amount = span > 0.0f ? (frame - left.frame) / span : 0.0f;
            return left.value + (right.value - left.value) * amount;
        }
    }
    return image.alpha_keys.back().value;
}
