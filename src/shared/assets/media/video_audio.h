#ifndef WALLPAPER_ENGINE_VIDEO_AUDIO_H
#define WALLPAPER_ENGINE_VIDEO_AUDIO_H

#include <cstdint>
#include <memory>

#include "shared/audio/audio_engine.h"

// Decodes the audio track of a video wallpaper and feeds it to an AudioEngine PCM stream.
// A feeder thread keeps the stream topped up (startFeeder) so render stalls cannot starve the audio
// device; the caller resyncs on video loop with restart(). pump() is also usable directly.
class VideoAudioStream {
   public:
    ~VideoAudioStream();
    VideoAudioStream(const VideoAudioStream&) = delete;
    VideoAudioStream& operator=(const VideoAudioStream&) = delete;

    static std::unique_ptr<VideoAudioStream> open(const char* path);

    bool hasAudio() const;
    // Resamples so playback runs `rate` times faster (pitch follows speed); call before pumping.
    void setRate(float rate);

    void pump(AudioEngine::StreamHandle stream, uint32_t target_queued_frames);
    void restart(AudioEngine::StreamHandle stream);
    void seek(AudioEngine::StreamHandle stream, double seconds);

    // Starts the background thread that pumps `stream` up to `target_queued_frames`. Idempotent.
    void startFeeder(AudioEngine::StreamHandle stream, uint32_t target_queued_frames);
    // Joins the thread; must run before the stream is destroyed.
    void stopFeeder();
    // While false the feeder idles (video not visible). Turning it off also drops queued audio.
    void setFeeding(AudioEngine::StreamHandle stream, bool feeding);

   private:
    VideoAudioStream() = default;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif  // WALLPAPER_ENGINE_VIDEO_AUDIO_H
