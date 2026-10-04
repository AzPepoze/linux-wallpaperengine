#ifndef WALLPAPER_ENGINE_MEDIA_SOURCE_H
#define WALLPAPER_ENGINE_MEDIA_SOURCE_H

#include <cstdint>
#include <memory>

struct AVIOContext;

namespace wallpaper_engine {

// Chooses where loose video files are read from. Disk (the default) streams from the file as ffmpeg asks, so a
// large video never has to fit in RAM; memory keeps one shared copy per file.
void setVideoLoadInRam(bool in_ram);

// Random-access bytes of one video. A TEX container's MP4 payload is exposed as if it were a plain file.
class MediaSource {
   public:
    virtual ~MediaSource() = default;
    virtual int64_t size() const = 0;
    // Returns the bytes read, 0 at the end of the data and a negative value on error.
    virtual int64_t readAt(int64_t offset, uint8_t* buffer, int length) const = 0;
};

// Opens a loose file or a file inside a mounted package. Returns null when it cannot be read.
std::unique_ptr<MediaSource> openMediaSource(const char* path);

// Adapts a MediaSource to ffmpeg's custom IO. Close it only after the AVFormatContext that uses it.
class MediaIo {
   public:
    MediaIo() = default;
    ~MediaIo();
    MediaIo(const MediaIo&) = delete;
    MediaIo& operator=(const MediaIo&) = delete;

    bool open(std::unique_ptr<MediaSource> source);
    void close();
    AVIOContext* context() const {
        return io_;
    }

   private:
    static int read(void* opaque, uint8_t* buffer, int length);
    static int64_t seek(void* opaque, int64_t offset, int whence);

    std::unique_ptr<MediaSource> source_;
    int64_t position_ = 0;
    AVIOContext* io_ = nullptr;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MEDIA_SOURCE_H
