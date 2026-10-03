#ifndef SHARED_AUDIO_AUDIO_ENGINE_H
#define SHARED_AUDIO_AUDIO_ENGINE_H

#include <stdint.h>

#include <memory>
#include <string>

// Process-wide audio backend (miniaudio): file playback, streaming PCM for video,
// and default-sink capture reduced to an audio spectrum for reactive effects.
class AudioEngine {
   public:
    static AudioEngine& instance();

    using SoundHandle = uint32_t;
    static constexpr SoundHandle kInvalidSound = 0;
    using StreamHandle = uint32_t;
    static constexpr StreamHandle kInvalidStream = 0;

    void init();
    void shutdown();
    bool isAvailable() const;
    // Disables all playback, capture and streaming before init(); used by automated runs.
    void setAudioDisabled(bool disabled);
    bool isAudioDisabled() const;

    SoundHandle play(const std::string& path, bool loop, float volume, bool start_paused = false);
    void stop(SoundHandle handle);
    bool isPlaying(SoundHandle handle) const;
    void setVolume(SoundHandle handle, float volume);
    void setMasterVolume(float volume);
    float masterVolume() const;

    // Streaming PCM pushed from a decoder. Samples are interleaved float32 at the stream's rate.
    StreamHandle createStream(uint32_t sample_rate, uint32_t channels);
    void destroyStream(StreamHandle handle);
    void pushStream(StreamHandle handle, const float* samples, uint32_t frame_count);
    void clearStream(StreamHandle handle);
    uint32_t streamQueuedFrames(StreamHandle handle) const;
    void setStreamMuted(StreamHandle handle, bool muted);
    // Stops draining the queue so buffered audio resumes exactly where it paused.
    void setStreamPaused(StreamHandle handle, bool paused);
    void setStreamVolume(StreamHandle handle, float volume);

    struct Spectrum {
        float bands16_left[16] = {};
        float bands16_right[16] = {};
        float bands32_left[32] = {};
        float bands32_right[32] = {};
        float bands64_left[64] = {};
        float bands64_right[64] = {};
    };

    // Advances spectrum smoothing; call once per frame on the main thread.
    void update(float dt);
    const Spectrum& spectrum() const;
    bool hasCapture() const;

   private:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif  // SHARED_AUDIO_AUDIO_ENGINE_H
