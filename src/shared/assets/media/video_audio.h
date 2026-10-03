#ifndef WALLPAPER_ENGINE_VIDEO_AUDIO_H
#define WALLPAPER_ENGINE_VIDEO_AUDIO_H

#include <cstdint>
#include <memory>

#include "shared/audio/audio_engine.h"

// Decodes the audio track of a video wallpaper and feeds it to an AudioEngine PCM stream.
// The caller drives decode with pump() and resyncs on video loop with restart().
class VideoAudioStream {
   public:
    ~VideoAudioStream();
    VideoAudioStream(const VideoAudioStream&) = delete;
    VideoAudioStream& operator=(const VideoAudioStream&) = delete;

    static std::unique_ptr<VideoAudioStream> open(const char* path);

    bool hasAudio() const;

    void pump(AudioEngine::StreamHandle stream, uint32_t target_queued_frames);
    void restart(AudioEngine::StreamHandle stream);

   private:
    VideoAudioStream() = default;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif  // WALLPAPER_ENGINE_VIDEO_AUDIO_H
