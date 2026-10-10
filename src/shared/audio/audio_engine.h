#ifndef SHARED_AUDIO_AUDIO_ENGINE_H
#define SHARED_AUDIO_AUDIO_ENGINE_H

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

// Process-wide audio backend: file playback, video PCM streams, and spectrum capture.
class AudioEngine {
   public:
    static AudioEngine& instance();

    using SoundHandle = uint32_t;
    static constexpr SoundHandle kInvalidSound = 0;
    using StreamHandle = uint32_t;
    static constexpr StreamHandle kInvalidStream = 0;

    using GroupId = uint32_t;
    static constexpr GroupId kDefaultGroup = 0;

    // Groups share a gain; kDefaultGroup always exists and is never destroyed.
    GroupId createGroup();
    void destroyGroup(GroupId group);
    void setGroupVolume(GroupId group, float volume);
    float groupVolume(GroupId group) const;
    // A fading stop() detaches the voice so the group gain can fade it out.
    void beginGroupFade(GroupId group);
    bool groupFading(GroupId group) const;
    void cancelGroupFade(GroupId group);

    struct Options {
        std::string device;   // name substring; empty or "default" = system default output
        bool capture = true;  // false skips the monitor capture, so the spectrum stays at zero
    };

    void init(const Options& options);
    void init() {
        init(Options{});
    }
    void shutdown();
    bool isAvailable() const;
    // Disables all playback, capture and streaming before init(); used by automated runs.
    void setAudioDisabled(bool disabled);
    bool isAudioDisabled() const;

    // Reopens playback on another device. Live sounds and streams are dropped; callers reload them.
    bool setPlaybackDevice(const std::string& device);
    const std::string& playbackDevice() const;
    void setCaptureEnabled(bool enabled);
    static std::vector<std::string> playbackDeviceNames();

    SoundHandle play(const std::string& path, bool loop, float volume, bool start_paused = false,
                     GroupId group = kDefaultGroup);
    void stop(SoundHandle handle);
    bool isPlaying(SoundHandle handle) const;
    void setVolume(SoundHandle handle, float volume);
    void setMasterVolume(float volume);
    float masterVolume() const;

    // Streaming PCM pushed from a decoder. Samples are interleaved float32 at the stream's rate.
    StreamHandle createStream(uint32_t sample_rate, uint32_t channels, GroupId group = kDefaultGroup);
    void destroyStream(StreamHandle handle);
    void pushStream(StreamHandle handle, const float* samples, uint32_t frame_count);
    void clearStream(StreamHandle handle);
    uint32_t streamQueuedFrames(StreamHandle handle) const;
    // Number of audio callbacks that had to zero-fill because the queue ran dry.
    uint32_t streamUnderruns(StreamHandle handle) const;
    // Number of decoded frames discarded because the queue was full.
    uint64_t streamDroppedFrames(StreamHandle handle) const;
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
    bool ensureContext();
    void openPlayback();
    void closePlayback();
    void openCapture();
    void closeCapture();
    void releaseSounds();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif  // SHARED_AUDIO_AUDIO_ENGINE_H
