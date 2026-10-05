#pragma once

#include <algorithm>

inline float introZoom(float start, float duration, float elapsed) {
    if (duration <= 0.0f) return 1.0f;
    const float t = std::clamp(elapsed / duration, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);
    return start + (1.0f - start) * eased;
}
