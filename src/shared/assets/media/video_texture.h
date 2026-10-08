#ifndef WALLPAPER_ENGINE_VIDEO_TEXTURE_H
#define WALLPAPER_ENGINE_VIDEO_TEXTURE_H

#include <cstdint>
#include <memory>
#include <vector>

#include "video_types.h"

struct ImportedVideoSurface;
struct AVFrame;

namespace wallpaper_engine {

class VideoTexture {
   public:
    ~VideoTexture();
    VideoTexture(const VideoTexture&) = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;

    static std::unique_ptr<VideoTexture> open(const char* texture_path);
    // Opens media and prepares decoder state without touching the graphics device.
    static std::unique_ptr<VideoTexture> openPrepared(const char* texture_path);
    // Initializes optional GPU import resources. Call on the graphics thread after openPrepared().
    void initializeGpu();
    static std::unique_ptr<VideoTexture> openFile(const char* video_path);

    uint32_t width() const;
    uint32_t height() const;
    float frameDuration() const;
    double fps() const;
    bool isZeroCopy() const;
    const std::string& codecName() const;
    const std::string& containerName() const;
    // Increments each time playback wraps to the start; used to resync audio.
    uint32_t loopCount() const;

    double duration() const;
    // Without looping, playback stops at the end of the file (loopCount() still counts it as an end).
    bool looping() const;
    void setLooping(bool enabled);
    // Back to the first frame, keeping the play state.
    void rewind();
    // Jump to `seconds` into the file, keeping the play state. False when the container cannot seek.
    bool seek(double seconds);

    void start();
    void stop();
    void pause();
    void resume();
    bool isPlaying() const;
    bool isPaused() const;

    bool decodeNextFrame(std::vector<uint8_t>& rgba_pixels);
    bool decodeNextFrameZeroCopy(ImportedVideoSurface*& out_surface, AVFrame*& out_av_frame);

    const ZeroCopyMetrics& getMetrics() const;
    const PlaybackStats& getStats() const;
    const PerformanceTiming& getTiming() const;

   private:
    VideoTexture() = default;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_VIDEO_TEXTURE_H
