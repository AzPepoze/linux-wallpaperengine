#include "audio_engine.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include "shared/core/logger.h"

// miniaudio is bundled as a header-only dependency; its implementation lives here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#pragma GCC diagnostic pop

namespace {
constexpr uint32_t kFftSize = 1024;
constexpr uint32_t kBandCount = 64;
constexpr float kSpectrumReleaseSeconds = 0.14f;
constexpr float kSpectrumFloorDb = -60.0f;

bool nameContains(const char* name, const char* needle) {
    if (!name || !needle) return false;
    const size_t needle_len = strlen(needle);
    if (needle_len == 0) return false;
    for (const char* p = name; *p; ++p) {
        size_t i = 0;
        while (i < needle_len && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == needle_len) return true;
    }
    return false;
}

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

// Reduce one channel of `kFftSize` samples into `kBandCount` log-spaced, decibel-mapped bands.
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

}  // namespace

struct AudioEngine::Impl {
    ma_engine engine = {};
    bool engine_ok = false;
    bool disabled = false;
    float master_volume = 1.0f;

    struct SoundSlot {
        ma_sound sound = {};
        bool active = false;
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

ma_data_source_vtable AudioEngine::Impl::streamVtable = {AudioEngine::Impl::streamRead,
                                                         AudioEngine::Impl::streamSeek,
                                                         AudioEngine::Impl::streamGetFormat,
                                                         AudioEngine::Impl::streamGetCursor,
                                                         AudioEngine::Impl::streamGetLength,
                                                         nullptr,
                                                         0};

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

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

void AudioEngine::init() {
    if (impl->disabled || impl->engine_ok) return;

    ma_engine_config engine_config = ma_engine_config_init();
    engine_config.channels = 2;
    engine_config.sampleRate = 48000;
    if (ma_engine_init(&engine_config, &impl->engine) == MA_SUCCESS) {
        impl->engine_ok = true;
        impl->master_volume = ma_engine_get_volume(&impl->engine);
        LOG_TAG_I("AUDIO", "Playback device opened (%u Hz, %u channels)", ma_engine_get_sample_rate(&impl->engine),
                  ma_engine_get_channels(&impl->engine));
    } else {
        LOG_TAG_W("AUDIO", "No playback device available; file and video audio will be silent");
    }

    if (ma_context_init(nullptr, 0, nullptr, &impl->capture_context) != MA_SUCCESS) {
        LOG_TAG_W("AUDIO", "Audio capture context unavailable; audio spectrum stays at zero");
        return;
    }
    impl->capture_context_ok = true;

    const ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;
    const ma_device_info* capture_devices = nullptr;
    ma_uint32 capture_count = 0;
    ma_context_get_devices(&impl->capture_context, (ma_device_info**)&playback_devices, &playback_count,
                           (ma_device_info**)&capture_devices, &capture_count);

    ma_device_info default_playback = {};
    const bool has_default = ma_context_get_device_info(&impl->capture_context, ma_device_type_playback, nullptr,
                                                        &default_playback) == MA_SUCCESS;

    const ma_device_info* monitor = nullptr;
    for (ma_uint32 i = 0; i < capture_count; ++i) {
        if (!nameContains(capture_devices[i].name, "monitor")) continue;
        if (monitor == nullptr) monitor = &capture_devices[i];
        if (has_default && nameContains(capture_devices[i].name, default_playback.name)) {
            monitor = &capture_devices[i];
            break;
        }
    }

    if (!monitor) {
        LOG_TAG_W("AUDIO", "No default sink monitor found; audio spectrum stays at zero");
        return;
    }

    ma_device_config capture_config = ma_device_config_init(ma_device_type_capture);
    capture_config.capture.format = ma_format_f32;
    capture_config.capture.channels = 2;
    capture_config.capture.pDeviceID = &monitor->id;
    capture_config.sampleRate = 48000;
    capture_config.dataCallback = Impl::captureCallback;
    capture_config.pUserData = impl.get();

    if (ma_device_init(&impl->capture_context, &capture_config, &impl->capture_device) != MA_SUCCESS ||
        ma_device_start(&impl->capture_device) != MA_SUCCESS) {
        LOG_TAG_W("AUDIO", "Failed to open monitor capture on '%s'; audio spectrum stays at zero", monitor->name);
        return;
    }

    impl->capture_ok = true;
    impl->capture_rate = impl->capture_device.sampleRate;
    impl->capture_channels = impl->capture_device.capture.channels;
    LOG_TAG_I("AUDIO", "Capture device opened on '%s' (%u Hz, %u channels, spectrum enabled)", monitor->name,
              impl->capture_rate, impl->capture_channels);
}

void AudioEngine::shutdown() {
    if (!impl) return;
    for (auto& stream : impl->streams) {
        if (!stream) continue;
        if (stream->sound_ready) ma_sound_uninit(&stream->sound);
        if (stream->rb_ready) ma_pcm_rb_uninit(&stream->rb);
        if (stream->base.vtable) ma_data_source_uninit(&stream->base);
    }
    impl->streams.clear();
    impl->stream_free.clear();

    for (auto& slot : impl->sound_slots) {
        if (slot && slot->active) ma_sound_uninit(&slot->sound);
    }
    impl->sound_slots.clear();
    impl->sound_free.clear();

    if (impl->capture_ok) {
        ma_device_uninit(&impl->capture_device);
        impl->capture_ok = false;
    }
    if (impl->capture_context_ok) {
        ma_context_uninit(&impl->capture_context);
        impl->capture_context_ok = false;
    }
    if (impl->engine_ok) {
        ma_engine_uninit(&impl->engine);
        impl->engine_ok = false;
    }
}

bool AudioEngine::isAvailable() const {
    return impl->engine_ok;
}

void AudioEngine::setAudioDisabled(bool disabled) {
    impl->disabled = disabled;
}

bool AudioEngine::isAudioDisabled() const {
    return impl->disabled;
}

AudioEngine::SoundHandle AudioEngine::play(const std::string& path, bool loop, float volume, bool start_paused) {
    if (!impl->engine_ok || path.empty()) return kInvalidSound;

    std::unique_ptr<Impl::SoundSlot> slot;
    SoundHandle handle = kInvalidSound;
    if (!impl->sound_free.empty()) {
        handle = impl->sound_free.back();
        impl->sound_free.pop_back();
        slot = std::move(impl->sound_slots[handle - 1]);
    } else {
        slot = std::make_unique<Impl::SoundSlot>();
        impl->sound_slots.push_back(nullptr);
        handle = (SoundHandle)impl->sound_slots.size();
    }

    if (ma_sound_init_from_file(&impl->engine, path.c_str(), 0, nullptr, nullptr, &slot->sound) != MA_SUCCESS) {
        LOG_TAG_W("AUDIO", "Failed to decode sound: %s", path.c_str());
        impl->sound_free.push_back(handle);
        return kInvalidSound;
    }
    slot->active = true;
    ma_sound_set_looping(&slot->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&slot->sound, std::max(0.0f, volume));
    if (!start_paused) ma_sound_start(&slot->sound);
    impl->sound_slots[handle - 1] = std::move(slot);
    LOG_TAG_I("AUDIO", "Sound started (loop=%d, volume=%.2f): %s", loop ? 1 : 0, volume, path.c_str());
    return handle;
}

void AudioEngine::stop(SoundHandle handle) {
    if (handle == kInvalidSound || handle > impl->sound_slots.size()) return;
    auto& slot = impl->sound_slots[handle - 1];
    if (!slot || !slot->active) return;
    ma_sound_stop(&slot->sound);
    ma_sound_uninit(&slot->sound);
    slot->active = false;
    impl->sound_free.push_back(handle);
}

bool AudioEngine::isPlaying(SoundHandle handle) const {
    if (handle == kInvalidSound || handle > impl->sound_slots.size()) return false;
    const auto& slot = impl->sound_slots[handle - 1];
    if (!slot || !slot->active) return false;
    return ma_sound_is_playing(&slot->sound) == MA_TRUE;
}

void AudioEngine::setVolume(SoundHandle handle, float volume) {
    if (handle == kInvalidSound || handle > impl->sound_slots.size()) return;
    auto& slot = impl->sound_slots[handle - 1];
    if (slot && slot->active) ma_sound_set_volume(&slot->sound, std::max(0.0f, volume));
}

void AudioEngine::setMasterVolume(float volume) {
    impl->master_volume = std::max(0.0f, volume);
    if (impl->engine_ok) ma_engine_set_volume(&impl->engine, impl->master_volume);
}

float AudioEngine::masterVolume() const {
    return impl->master_volume;
}

AudioEngine::StreamHandle AudioEngine::createStream(uint32_t sample_rate, uint32_t channels) {
    if (impl->disabled) return kInvalidStream;
    if (channels == 0) channels = 2;
    if (sample_rate == 0) sample_rate = 48000;

    auto stream = std::make_unique<Impl::Stream>();
    stream->channels = channels;
    stream->sample_rate = sample_rate;
    if (ma_pcm_rb_init(ma_format_f32, channels, sample_rate / 2, nullptr, nullptr, &stream->rb) != MA_SUCCESS) {
        return kInvalidStream;
    }
    stream->rb_ready = true;

    ma_data_source_config source_config = ma_data_source_config_init();
    source_config.vtable = &Impl::streamVtable;
    if (ma_data_source_init(&source_config, &stream->base) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&stream->rb);
        return kInvalidStream;
    }

    if (impl->engine_ok &&
        ma_sound_init_from_data_source(&impl->engine, &stream->base, 0, nullptr, &stream->sound) == MA_SUCCESS) {
        stream->sound_ready = true;
        ma_sound_set_volume(&stream->sound, 1.0f);
        ma_sound_start(&stream->sound);
    }

    StreamHandle handle = kInvalidStream;
    if (!impl->stream_free.empty()) {
        handle = impl->stream_free.back();
        impl->stream_free.pop_back();
        impl->streams[handle - 1] = std::move(stream);
    } else {
        impl->streams.push_back(std::move(stream));
        handle = (StreamHandle)impl->streams.size();
    }
    LOG_TAG_I("AUDIO", "PCM stream created (%u Hz, %u channels)", sample_rate, channels);
    return handle;
}

void AudioEngine::destroyStream(StreamHandle handle) {
    if (handle == kInvalidStream || handle > impl->streams.size()) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream) return;
    if (stream->sound_ready) ma_sound_uninit(&stream->sound);
    if (stream->rb_ready) ma_pcm_rb_uninit(&stream->rb);
    if (stream->base.vtable) ma_data_source_uninit(&stream->base);
    stream.reset();
    impl->stream_free.push_back(handle);
}

void AudioEngine::pushStream(StreamHandle handle, const float* samples, uint32_t frame_count) {
    if (handle == kInvalidStream || handle > impl->streams.size() || !samples) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream) return;
    const size_t frame_bytes = (size_t)stream->channels * sizeof(float);
    const auto* src = reinterpret_cast<const uint8_t*>(samples);
    uint32_t remaining = frame_count;
    while (remaining > 0) {
        ma_uint32 to_write = remaining;
        void* write_ptr = nullptr;
        if (ma_pcm_rb_acquire_write(&stream->rb, &to_write, &write_ptr) != MA_SUCCESS || to_write == 0) break;
        memcpy(write_ptr, src, (size_t)to_write * frame_bytes);
        ma_pcm_rb_commit_write(&stream->rb, to_write);
        src += (size_t)to_write * frame_bytes;
        remaining -= to_write;
    }
}

void AudioEngine::clearStream(StreamHandle handle) {
    if (handle == kInvalidStream || handle > impl->streams.size()) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream) return;
    ma_pcm_rb_reset(&stream->rb);
}

uint32_t AudioEngine::streamQueuedFrames(StreamHandle handle) const {
    if (handle == kInvalidStream || handle > impl->streams.size()) return 0;
    const auto& stream = impl->streams[handle - 1];
    if (!stream) return 0;
    return ma_pcm_rb_available_read(const_cast<ma_pcm_rb*>(&stream->rb));
}

void AudioEngine::setStreamMuted(StreamHandle handle, bool muted) {
    if (handle == kInvalidStream || handle > impl->streams.size()) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream) return;
    stream->muted = muted;
    if (stream->sound_ready) ma_sound_set_volume(&stream->sound, muted ? 0.0f : stream->volume);
}

void AudioEngine::setStreamVolume(StreamHandle handle, float volume) {
    if (handle == kInvalidStream || handle > impl->streams.size()) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream) return;
    stream->volume = std::max(0.0f, volume);
    if (stream->sound_ready) ma_sound_set_volume(&stream->sound, stream->muted ? 0.0f : stream->volume);
}

void AudioEngine::update(float dt) {
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
