#include <math.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include "audio_engine_internal.h"
#include "shared/core/logger.h"

using namespace audio_engine_internal;

namespace {
constexpr float kSpectrumReleaseSeconds = 0.14f;
constexpr float kSpectrumFloorDb = -60.0f;

void fftRadix2(float* re, float* im, uint32_t n) {
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * (float)M_PI / (float)len;
        const float w_step_re = cosf(angle);
        const float w_step_im = sinf(angle);
        for (uint32_t i = 0; i < n; i += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (uint32_t k = 0; k < len / 2; ++k) {
                const float u_re = re[i + k];
                const float u_im = im[i + k];
                const float v_re = re[i + k + len / 2] * w_re - im[i + k + len / 2] * w_im;
                const float v_im = re[i + k + len / 2] * w_im + im[i + k + len / 2] * w_re;
                re[i + k] = u_re + v_re;
                im[i + k] = u_im + v_im;
                re[i + k + len / 2] = u_re - v_re;
                im[i + k + len / 2] = u_im - v_im;
                const float next_re = w_re * w_step_re - w_im * w_step_im;
                w_im = w_re * w_step_im + w_im * w_step_re;
                w_re = next_re;
            }
        }
    }
}
}  // namespace

namespace audio_engine_internal {
void computeBands(const float* samples, uint32_t sample_rate, float* out_bands) {
    float re[kFftSize];
    float im[kFftSize];
    for (uint32_t i = 0; i < kFftSize; ++i) {
        const float window = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)(kFftSize - 1));
        re[i] = samples[i] * window;
        im[i] = 0.0f;
    }
    fftRadix2(re, im, kFftSize);

    const uint32_t half = kFftSize / 2;
    float magnitude[half + 1];
    for (uint32_t i = 0; i <= half; ++i) magnitude[i] = sqrtf(re[i] * re[i] + im[i] * im[i]);

    const float nyquist = (float)sample_rate * 0.5f;
    const float f_min = 30.0f;
    const float f_max = std::min(16000.0f, nyquist);
    const float ratio = f_max / f_min;
    for (uint32_t band = 0; band < kBandCount; ++band) {
        const float lo_hz = f_min * powf(ratio, (float)band / (float)kBandCount);
        const float hi_hz = f_min * powf(ratio, (float)(band + 1) / (float)kBandCount);
        uint32_t lo_bin = (uint32_t)floorf(lo_hz * (float)kFftSize / (float)sample_rate);
        uint32_t hi_bin = (uint32_t)ceilf(hi_hz * (float)kFftSize / (float)sample_rate);
        lo_bin = std::min(std::max(lo_bin, 1u), half);
        hi_bin = std::min(std::max(hi_bin, lo_bin + 1), half + 1);

        float peak = 0.0f;
        for (uint32_t bin = lo_bin; bin < hi_bin; ++bin) peak = std::max(peak, magnitude[bin]);
        const float db = 20.0f * log10f(peak / (float)kFftSize + 1e-7f);
        out_bands[band] = std::min(1.0f, std::max(0.0f, (db - kSpectrumFloorDb) / -kSpectrumFloorDb));
    }
}
}  // namespace audio_engine_internal

void AudioEngine::Impl::captureCallback(ma_device* device, void*, const void* input, ma_uint32 frame_count) {
    auto* impl = static_cast<Impl*>(device->pUserData);
    if (!impl || !input || frame_count == 0 || impl->capture_channels == 0) return;
    const auto* samples = static_cast<const float*>(input);
    const uint32_t channels = impl->capture_channels;
    const uint32_t copied = std::min(frame_count, kFftSize);
    const uint32_t offset = frame_count - copied;

    std::lock_guard<std::mutex> lock(impl->capture_mutex);
    if (copied == kFftSize) {
        for (uint32_t frame = 0; frame < copied; ++frame) {
            impl->window_left[frame] = samples[(size_t)(offset + frame) * channels];
            impl->window_right[frame] =
                channels > 1 ? samples[(size_t)(offset + frame) * channels + 1] : impl->window_left[frame];
        }
    } else {
        memmove(impl->window_left, impl->window_left + copied, sizeof(float) * (kFftSize - copied));
        memmove(impl->window_right, impl->window_right + copied, sizeof(float) * (kFftSize - copied));
        for (uint32_t frame = 0; frame < copied; ++frame) {
            impl->window_left[kFftSize - copied + frame] = samples[(size_t)(offset + frame) * channels];
            impl->window_right[kFftSize - copied + frame] = channels > 1
                                                                ? samples[(size_t)(offset + frame) * channels + 1]
                                                                : impl->window_left[kFftSize - copied + frame];
        }
    }

    if (frame_count >= kFftSize / 8) {
        computeBands(impl->window_left, impl->capture_rate, impl->pending_left);
        computeBands(impl->window_right, impl->capture_rate, impl->pending_right);
        impl->pending_valid = true;
    }
}

void AudioEngine::update(float dt) {
    // Closing capture already zeroed the bands, so there is nothing to smooth.
    if (!impl->capture_ok) return;
    float target_left[kBandCount] = {};
    float target_right[kBandCount] = {};
    bool has_target = false;
    {
        std::lock_guard<std::mutex> lock(impl->capture_mutex);
        if (impl->pending_valid) {
            memcpy(target_left, impl->pending_left, sizeof(target_left));
            memcpy(target_right, impl->pending_right, sizeof(target_right));
            has_target = true;
        }
    }

    const float release = dt > 0.0f ? expf(-dt / kSpectrumReleaseSeconds) : 0.85f;
    if (has_target && !impl->capture_logged_signal) {
        float peak = 0.0f;
        for (int i = 0; i < 64; ++i) peak = std::max(peak, std::max(target_left[i], target_right[i]));
        if (peak > 0.01f) {
            impl->capture_logged_signal = true;
            LOG_TAG_I("AUDIO", "Audio spectrum capture is live (level detected)");
        }
    }
    auto smooth = [&](float* smoothed, const float* target, int count) {
        for (int i = 0; i < count; ++i) {
            const float value = has_target ? target[i] : 0.0f;
            smoothed[i] = value > smoothed[i] ? value : smoothed[i] * release;
        }
    };
    smooth(impl->spectrum.bands64_left, target_left, 64);
    smooth(impl->spectrum.bands64_right, target_right, 64);
    for (int i = 0; i < 32; ++i) {
        impl->spectrum.bands32_left[i] =
            0.5f * (impl->spectrum.bands64_left[i * 2] + impl->spectrum.bands64_left[i * 2 + 1]);
        impl->spectrum.bands32_right[i] =
            0.5f * (impl->spectrum.bands64_right[i * 2] + impl->spectrum.bands64_right[i * 2 + 1]);
    }
    for (int i = 0; i < 16; ++i) {
        impl->spectrum.bands16_left[i] =
            0.25f * (impl->spectrum.bands64_left[i * 4] + impl->spectrum.bands64_left[i * 4 + 1] +
                     impl->spectrum.bands64_left[i * 4 + 2] + impl->spectrum.bands64_left[i * 4 + 3]);
        impl->spectrum.bands16_right[i] =
            0.25f * (impl->spectrum.bands64_right[i * 4] + impl->spectrum.bands64_right[i * 4 + 1] +
                     impl->spectrum.bands64_right[i * 4 + 2] + impl->spectrum.bands64_right[i * 4 + 3]);
    }
}

const AudioEngine::Spectrum& AudioEngine::spectrum() const {
    return impl->spectrum;
}

bool AudioEngine::hasCapture() const {
    return impl->capture_ok;
}
