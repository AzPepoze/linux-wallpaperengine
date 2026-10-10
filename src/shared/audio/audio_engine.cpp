// miniaudio is bundled as a header-only dependency; its implementation lives here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#pragma GCC diagnostic pop

#include <ctype.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

#include "audio_engine_internal.h"
#include "ogg_decoder.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"

namespace {
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

bool isDefaultDevice(const std::string& name) {
    return name.empty() || name == "default";
}

bool findPlaybackId(ma_context& context, const std::string& name, ma_device_id& out) {
    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_devices = nullptr;
    ma_uint32 capture_count = 0;
    ma_context_get_devices(&context, &playback_devices, &playback_count, &capture_devices, &capture_count);
    for (ma_uint32 i = 0; i < playback_count; ++i) {
        if (!nameContains(playback_devices[i].name, name.c_str())) continue;
        out = playback_devices[i].id;
        return true;
    }
    return false;
}
}  // namespace

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

bool AudioEngine::ensureContext() {
    if (impl->context_ok) return true;
    if (ma_context_init(nullptr, 0, nullptr, &impl->context) != MA_SUCCESS) return false;
    impl->context_ok = true;
    return true;
}

void AudioEngine::init(const Options& options) {
    if (impl->disabled || impl->engine_ok) return;
    impl->device_name = options.device;
    impl->capture_wanted = options.capture;
    openPlayback();
    openCapture();
}

void AudioEngine::openPlayback() {
    impl->has_playback_id = false;
    if (!ensureContext()) {
        LOG_TAG_W("AUDIO", "Audio context unavailable; file and video audio will be silent");
        return;
    }

    ma_engine_config engine_config = ma_engine_config_init();
    engine_config.channels = 2;
    engine_config.sampleRate = 48000;
    engine_config.pContext = &impl->context;
    if (!isDefaultDevice(impl->device_name)) {
        impl->has_playback_id = findPlaybackId(impl->context, impl->device_name, impl->playback_id);
        if (!impl->has_playback_id)
            LOG_TAG_W("AUDIO", "No playback device matches '%s'; using the default", impl->device_name.c_str());
    }
    if (impl->has_playback_id) engine_config.pPlaybackDeviceID = &impl->playback_id;

    if (ma_engine_init(&engine_config, &impl->engine) != MA_SUCCESS) {
        LOG_TAG_W("AUDIO", "No playback device available; file and video audio will be silent");
        return;
    }
    impl->engine_ok = true;
    ma_engine_set_volume(&impl->engine, impl->master_volume);
    LOG_TAG_I("AUDIO", "Playback device opened (%u Hz, %u channels)", ma_engine_get_sample_rate(&impl->engine),
              ma_engine_get_channels(&impl->engine));
}

void AudioEngine::closePlayback() {
    if (!impl->engine_ok) return;
    ma_engine_uninit(&impl->engine);
    impl->engine_ok = false;
}

void AudioEngine::openCapture() {
    if (impl->capture_ok || !impl->capture_wanted || !ensureContext()) return;

    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_devices = nullptr;
    ma_uint32 capture_count = 0;
    ma_context_get_devices(&impl->context, &playback_devices, &playback_count, &capture_devices, &capture_count);

    // The monitor of the output we play on, falling back to the system default output.
    const ma_device_id* output_id = impl->has_playback_id ? &impl->playback_id : nullptr;
    ma_device_info output = {};
    const bool has_output =
        ma_context_get_device_info(&impl->context, ma_device_type_playback, output_id, &output) == MA_SUCCESS;

    const ma_device_info* monitor = nullptr;
    for (ma_uint32 i = 0; i < capture_count; ++i) {
        if (!nameContains(capture_devices[i].name, "monitor")) continue;
        if (monitor == nullptr) monitor = &capture_devices[i];
        if (has_output && nameContains(capture_devices[i].name, output.name)) {
            monitor = &capture_devices[i];
            break;
        }
    }

    if (!monitor) {
        LOG_TAG_W("AUDIO", "No output monitor found; audio spectrum stays at zero");
        return;
    }

    ma_device_config capture_config = ma_device_config_init(ma_device_type_capture);
    capture_config.capture.format = ma_format_f32;
    capture_config.capture.channels = 2;
    capture_config.capture.pDeviceID = &monitor->id;
    capture_config.sampleRate = 48000;
    capture_config.dataCallback = Impl::captureCallback;
    capture_config.pUserData = impl.get();

    if (ma_device_init(&impl->context, &capture_config, &impl->capture_device) != MA_SUCCESS ||
        ma_device_start(&impl->capture_device) != MA_SUCCESS) {
        LOG_TAG_W("AUDIO", "Failed to open monitor capture on '%s'; audio spectrum stays at zero", monitor->name);
        return;
    }

    impl->capture_ok = true;
    impl->capture_logged_signal = false;
    impl->capture_rate = impl->capture_device.sampleRate;
    impl->capture_channels = impl->capture_device.capture.channels;
    LOG_TAG_I("AUDIO", "Capture device opened on '%s' (%u Hz, %u channels, spectrum enabled)", monitor->name,
              impl->capture_rate, impl->capture_channels);
}

void AudioEngine::closeCapture() {
    if (impl->capture_ok) {
        ma_device_uninit(&impl->capture_device);
        impl->capture_ok = false;
    }
    impl->pending_valid = false;
    impl->spectrum = Spectrum{};
}

void AudioEngine::releaseSounds() {
    std::unique_lock<std::mutex> stream_lock(impl->stream_mutex);
    for (auto& stream : impl->streams) {
        if (!stream) continue;
        if (stream->sound_ready) ma_sound_uninit(&stream->sound);
        if (stream->rb_ready) ma_pcm_rb_uninit(&stream->rb);
        if (stream->base.vtable) ma_data_source_uninit(&stream->base);
    }
    impl->streams.clear();
    impl->stream_free.clear();
    stream_lock.unlock();

    for (auto& slot : impl->sound_slots) {
        if (slot) slot->shutdown();
    }
    impl->sound_slots.clear();
    impl->sound_free.clear();
}

void AudioEngine::shutdown() {
    if (!impl) return;
    releaseSounds();

    impl->groups = {Impl::GroupState{}};
    impl->group_free.clear();

    closeCapture();
    closePlayback();
    if (impl->context_ok) {
        ma_context_uninit(&impl->context);
        impl->context_ok = false;
    }
}

bool AudioEngine::setPlaybackDevice(const std::string& device) {
    impl->device_name = device;
    if (impl->disabled) return false;
    // Sounds and streams belong to the old engine; the caller reloads the wallpaper that owns them.
    releaseSounds();
    closeCapture();
    closePlayback();
    openPlayback();
    openCapture();
    return impl->engine_ok;
}

const std::string& AudioEngine::playbackDevice() const {
    return impl->device_name;
}

void AudioEngine::setCaptureEnabled(bool enabled) {
    impl->capture_wanted = enabled;
    if (enabled) {
        openCapture();
    } else {
        closeCapture();
    }
}

std::vector<std::string> AudioEngine::playbackDeviceNames() {
    std::vector<std::string> names;
    ma_context context = {};
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return names;

    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_devices = nullptr;
    ma_uint32 capture_count = 0;
    ma_context_get_devices(&context, &playback_devices, &playback_count, &capture_devices, &capture_count);
    for (ma_uint32 i = 0; i < playback_count; ++i) names.push_back(playback_devices[i].name);

    ma_context_uninit(&context);
    return names;
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

AudioEngine::GroupId AudioEngine::createGroup() {
    GroupId id;
    if (!impl->group_free.empty()) {
        id = impl->group_free.back();
        impl->group_free.pop_back();
        impl->groups[id] = Impl::GroupState{};
    } else {
        impl->groups.push_back(Impl::GroupState{});
        id = (GroupId)(impl->groups.size() - 1);
    }
    return id;
}

void AudioEngine::destroyGroup(GroupId group) {
    if (group == kDefaultGroup || group >= impl->groups.size()) return;
    for (size_t i = 0; i < impl->sound_slots.size(); ++i) {
        auto& slot = impl->sound_slots[i];
        if (slot && slot->active && slot->group == group) {
            ma_sound_stop(&slot->sound);
            slot->shutdown();
            impl->sound_free.push_back((SoundHandle)(i + 1));
        }
    }
    std::lock_guard<std::mutex> stream_lock(impl->stream_mutex);
    for (size_t i = 0; i < impl->streams.size(); ++i) {
        auto& stream = impl->streams[i];
        if (!stream || stream->group != group) continue;
        if (stream->sound_ready) ma_sound_uninit(&stream->sound);
        if (stream->rb_ready) ma_pcm_rb_uninit(&stream->rb);
        if (stream->base.vtable) ma_data_source_uninit(&stream->base);
        stream.reset();
        impl->stream_free.push_back((StreamHandle)(i + 1));
    }
    impl->groups[group] = Impl::GroupState{};
    impl->group_free.push_back(group);
}

void AudioEngine::setGroupVolume(GroupId group, float volume) {
    if (group >= impl->groups.size()) return;
    impl->groups[group].volume = std::max(0.0f, volume);
    const float gain = impl->groups[group].volume;
    for (size_t i = 0; i < impl->sound_slots.size(); ++i) {
        auto& slot = impl->sound_slots[i];
        if (slot && slot->active && slot->group == group) ma_sound_set_volume(&slot->sound, slot->base_volume * gain);
    }
    for (auto& stream : impl->streams) {
        if (stream && stream->group == group && stream->sound_ready)
            ma_sound_set_volume(&stream->sound, stream->muted ? 0.0f : stream->volume * gain);
    }
}

float AudioEngine::groupVolume(GroupId group) const {
    if (group >= impl->groups.size()) return 1.0f;
    return impl->groups[group].volume;
}

void AudioEngine::beginGroupFade(GroupId group) {
    if (group < impl->groups.size()) impl->groups[group].fading = true;
}

bool AudioEngine::groupFading(GroupId group) const {
    return group < impl->groups.size() && impl->groups[group].fading;
}

void AudioEngine::cancelGroupFade(GroupId group) {
    if (group < impl->groups.size()) impl->groups[group].fading = false;
}

AudioEngine::SoundHandle AudioEngine::play(const std::string& path, bool loop, float volume, bool start_paused,
                                           GroupId group) {
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

    const uint8_t* packaged = nullptr;
    size_t packaged_size = 0;
    bool loaded = false;
    if (vfs::find(path.c_str(), packaged, packaged_size)) {
        slot->has_decoder = ma_decoder_init_memory(packaged, packaged_size, nullptr, &slot->decoder) == MA_SUCCESS;
        loaded = slot->has_decoder &&
                 ma_sound_init_from_data_source(&impl->engine, &slot->decoder, 0, nullptr, &slot->sound) == MA_SUCCESS;
        if (!loaded && slot->has_decoder) {
            ma_decoder_uninit(&slot->decoder);
            slot->has_decoder = false;
        }
    } else {
        loaded = ma_sound_init_from_file(&impl->engine, path.c_str(), 0, nullptr, nullptr, &slot->sound) == MA_SUCCESS;
    }
    if (!loaded) {
        std::vector<uint8_t> file_bytes;
        const uint8_t* bytes = packaged;
        size_t byte_count = packaged_size;
        if (!bytes) {
            std::ifstream file(path, std::ios::binary);
            file_bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            bytes = file_bytes.data();
            byte_count = file_bytes.size();
        }
        uint32_t channels = 0;
        uint32_t sample_rate = 0;
        if (decodeOggVorbis(bytes, byte_count, slot->vorbis_pcm, channels, sample_rate)) {
            ma_audio_buffer_config buffer_config = ma_audio_buffer_config_init(
                ma_format_s16, channels, slot->vorbis_pcm.size() / channels, slot->vorbis_pcm.data(), nullptr);
            buffer_config.sampleRate = sample_rate;
            slot->has_vorbis_buffer = ma_audio_buffer_init(&buffer_config, &slot->vorbis_buffer) == MA_SUCCESS;
            loaded = slot->has_vorbis_buffer && ma_sound_init_from_data_source(&impl->engine, &slot->vorbis_buffer, 0,
                                                                               nullptr, &slot->sound) == MA_SUCCESS;
            if (!loaded && slot->has_vorbis_buffer) ma_audio_buffer_uninit(&slot->vorbis_buffer);
            if (!loaded) {
                slot->has_vorbis_buffer = false;
                slot->vorbis_pcm.clear();
            }
        }
    }
    if (!loaded) {
        LOG_TAG_W("AUDIO", "Failed to decode sound: %s", path.c_str());
        impl->sound_slots[handle - 1] = std::move(slot);
        impl->sound_free.push_back(handle);
        return kInvalidSound;
    }
    slot->active = true;
    slot->group = group;
    slot->base_volume = std::max(0.0f, volume);
    ma_sound_set_looping(&slot->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&slot->sound, slot->base_volume * groupVolume(group));
    if (!start_paused) ma_sound_start(&slot->sound);
    impl->sound_slots[handle - 1] = std::move(slot);
    LOG_TAG_I("AUDIO", "Sound started (loop=%d, volume=%.2f, group=%u): %s", loop ? 1 : 0, volume, (unsigned)group,
              path.c_str());
    return handle;
}

void AudioEngine::stop(SoundHandle handle) {
    if (handle == kInvalidSound || handle > impl->sound_slots.size()) return;
    auto& slot = impl->sound_slots[handle - 1];
    if (!slot || !slot->active) return;
    if (groupFading(slot->group)) return;  // detached; destroyGroup() cleans up
    ma_sound_stop(&slot->sound);
    slot->shutdown();
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
    if (slot && slot->active) {
        slot->base_volume = std::max(0.0f, volume);
        ma_sound_set_volume(&slot->sound, slot->base_volume * groupVolume(slot->group));
    }
}

void AudioEngine::setMasterVolume(float volume) {
    impl->master_volume = std::max(0.0f, volume);
    if (impl->engine_ok) ma_engine_set_volume(&impl->engine, impl->master_volume);
}

float AudioEngine::masterVolume() const {
    return impl->master_volume;
}
