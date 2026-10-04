#include <math.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include "audio_engine_internal.h"
#include "shared/core/logger.h"

ma_data_source_vtable AudioEngine::Impl::streamVtable = {AudioEngine::Impl::streamRead,
                                                         AudioEngine::Impl::streamSeek,
                                                         AudioEngine::Impl::streamGetFormat,
                                                         AudioEngine::Impl::streamGetCursor,
                                                         AudioEngine::Impl::streamGetLength,
                                                         nullptr,
                                                         0};

AudioEngine::StreamHandle AudioEngine::createStream(uint32_t sample_rate, uint32_t channels, GroupId group) {
    if (impl->disabled) return kInvalidStream;
    if (channels == 0) channels = 2;
    if (sample_rate == 0) sample_rate = 48000;

    auto stream = std::make_unique<Impl::Stream>();
    stream->channels = channels;
    stream->sample_rate = sample_rate;
    stream->group = group;
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
        ma_sound_set_volume(&stream->sound, stream->volume * groupVolume(group));
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
    if (stream->sound_ready)
        ma_sound_set_volume(&stream->sound, muted ? 0.0f : stream->volume * groupVolume(stream->group));
}

void AudioEngine::setStreamPaused(StreamHandle handle, bool paused) {
    if (handle == kInvalidStream || handle > impl->streams.size()) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream || !stream->sound_ready) return;
    if (paused)
        ma_sound_stop(&stream->sound);
    else
        ma_sound_start(&stream->sound);
}

void AudioEngine::setStreamVolume(StreamHandle handle, float volume) {
    if (handle == kInvalidStream || handle > impl->streams.size()) return;
    auto& stream = impl->streams[handle - 1];
    if (!stream) return;
    stream->volume = std::max(0.0f, volume);
    if (stream->sound_ready)
        ma_sound_set_volume(&stream->sound, stream->muted ? 0.0f : stream->volume * groupVolume(stream->group));
}
