#ifndef SHARED_AUDIO_AUDIO_ENGINE_INTERNAL_H
#define SHARED_AUDIO_AUDIO_ENGINE_INTERNAL_H

#include <stdint.h>

#include <memory>
#include <mutex>
#include <vector>

#include "audio_engine.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <miniaudio.h>
#pragma GCC diagnostic pop

namespace audio_engine_internal {
constexpr uint32_t kFftSize = 1024;
constexpr uint32_t kBandCount = 64;

// Reduce one channel of `kFftSize` samples into `kBandCount` log-spaced, decibel-mapped bands.
void computeBands(const float* samples, uint32_t sample_rate, float* out_bands);
}  // namespace audio_engine_internal

using audio_engine_internal::kBandCount;
using audio_engine_internal::kFftSize;

struct AudioEngine::Impl {
    ma_engine engine = {};
    bool engine_ok = false;
    bool disabled = false;
    float master_volume = 1.0f;

    struct SoundSlot {
        ma_sound sound = {};
        ma_decoder decoder = {};  // Backs `sound` when it plays from a mapped package file.
        bool has_decoder = false;
        bool active = false;

        void release() {
            if (!active) return;
            ma_sound_uninit(&sound);
            if (has_decoder) ma_decoder_uninit(&decoder);
            has_decoder = false;
            active = false;
        }
    };
    std::vector<std::unique_ptr<SoundSlot>> sound_slots;
    std::vector<SoundHandle> sound_free;

    struct Stream {
        ma_data_source_base base = {};
        ma_pcm_rb rb = {};
        ma_sound sound = {};
        uint32_t channels = 2;
        uint32_t sample_rate = 48000;
        float volume = 1.0f;
        bool muted = false;
        bool sound_ready = false;
        bool rb_ready = false;
    };
    std::vector<std::unique_ptr<Stream>> streams;
    std::vector<StreamHandle> stream_free;

    ma_context capture_context = {};
    ma_device capture_device = {};
    bool capture_context_ok = false;
    bool capture_ok = false;
    uint32_t capture_rate = 48000;
    uint32_t capture_channels = 2;

    std::mutex capture_mutex;
    float window_left[kFftSize] = {};
    float window_right[kFftSize] = {};
    float pending_left[kBandCount] = {};
    float pending_right[kBandCount] = {};
    bool pending_valid = false;

    Spectrum spectrum;
    bool capture_logged_signal = false;

    static ma_result streamRead(ma_data_source* data_source, void* frames_out, ma_uint64 frame_count,
                                ma_uint64* frames_read) {
        auto* stream = reinterpret_cast<Stream*>(data_source);
        auto* out = static_cast<uint8_t*>(frames_out);
        const size_t frame_bytes = (size_t)stream->channels * sizeof(float);
        ma_uint64 total = 0;
        while (total < frame_count) {
            ma_uint32 available = ma_pcm_rb_available_read(&stream->rb);
            if (available == 0) break;
            void* read_ptr = nullptr;
            ma_uint32 to_read = (ma_uint32)std::min<ma_uint64>(available, frame_count - total);
            if (ma_pcm_rb_acquire_read(&stream->rb, &to_read, &read_ptr) != MA_SUCCESS || to_read == 0) break;
            memcpy(out + total * frame_bytes, read_ptr, (size_t)to_read * frame_bytes);
            ma_pcm_rb_commit_read(&stream->rb, to_read);
            total += to_read;
        }
        if (total < frame_count) {
            memset(out + total * frame_bytes, 0, (size_t)(frame_count - total) * frame_bytes);
        }
        *frames_read = frame_count;
        return MA_SUCCESS;
    }

    static ma_result streamSeek(ma_data_source*, ma_uint64) {
        return MA_NOT_IMPLEMENTED;
    }

    static ma_result streamGetFormat(ma_data_source* data_source, ma_format* format, ma_uint32* channels,
                                     ma_uint32* sample_rate, ma_channel*, size_t) {
        auto* stream = reinterpret_cast<Stream*>(data_source);
        if (format) *format = ma_format_f32;
        if (channels) *channels = stream->channels;
        if (sample_rate) *sample_rate = stream->sample_rate;
        return MA_SUCCESS;
    }

    static ma_result streamGetCursor(ma_data_source*, ma_uint64* cursor) {
        if (cursor) *cursor = 0;
        return MA_NOT_IMPLEMENTED;
    }

    static ma_result streamGetLength(ma_data_source*, ma_uint64* length) {
        if (length) *length = 0;
        return MA_NOT_IMPLEMENTED;
    }

    static ma_data_source_vtable streamVtable;
    static void captureCallback(ma_device* device, void*, const void* input, ma_uint32 frame_count);
};

#endif  // SHARED_AUDIO_AUDIO_ENGINE_INTERNAL_H
