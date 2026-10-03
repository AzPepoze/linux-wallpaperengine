#ifndef WALLPAPER_ENGINE_VIDEO_RATE_H
#define WALLPAPER_ENGINE_VIDEO_RATE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float kMinPlaybackRate = 0.25f;
constexpr float kMaxPlaybackRate = 4.0f;

inline float clampPlaybackRate(float rate) {
    return std::isfinite(rate) ? std::clamp(rate, kMinPlaybackRate, kMaxPlaybackRate) : 1.0f;
}

// Audio is resampled so the engine's fixed-rate stream plays the track `rate` times faster.
inline uint32_t resampledAudioRate(uint32_t stream_rate, float rate) {
    return (uint32_t)std::lround((double)stream_rate / (double)clampPlaybackRate(rate));
}

#endif  // WALLPAPER_ENGINE_VIDEO_RATE_H
