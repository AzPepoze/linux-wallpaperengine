#ifndef WALLPAPER_ENGINE_MEDIA_SESSION_H
#define WALLPAPER_ENGINE_MEDIA_SESSION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wallpaper_engine {

enum class PlaybackState { Stopped, Playing, Paused };

struct MediaProperties {
    std::string title;
    std::string artist;
    std::string albumTitle;
    std::string albumArtist;
    std::string subTitle;
    std::string genres;
    std::string contentType;
};

struct ThumbnailColors {
    bool has_thumbnail = false;
    float primary[3]{};
    float secondary[3]{};
    float tertiary[3]{};
    float text[3]{};
    float high_contrast[3]{};
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

struct MediaEvent {
    enum class Kind { Status, Playback, Properties, Thumbnail, Timeline };

    Kind kind = Kind::Status;
    bool enabled = false;
    PlaybackState state = PlaybackState::Stopped;
    MediaProperties properties;
    ThumbnailColors thumbnail;
    double position = 0;
    double duration = 0;
};

class MediaSource {
   public:
    virtual ~MediaSource() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    // Drain events produced since the last call. Main thread only.
    virtual std::vector<MediaEvent> poll() = 0;
};

// Returns a no-op source (never emits) when built without sd-bus support.
std::unique_ptr<MediaSource> createMprisMediaSource();

// Sets the no-cover artwork; call before the media source starts so the worker sees a stable value.
void setMediaThumbnailFallbackImage(const std::string& path);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MEDIA_SESSION_H
