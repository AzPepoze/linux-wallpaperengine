#include "media_source.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "shared/core/vfs.h"

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace wallpaper_engine {
namespace {
constexpr size_t kProbeBytes = 64 * 1024;
constexpr int kIoBufferSize = 128 * 1024;

std::atomic<bool> g_load_in_ram{false};

// A TEX video stores a plain MP4 after its header; the box starts 4 bytes before the "ftyp" tag.
int64_t payloadOffset(const uint8_t* head, size_t length) {
    static constexpr uint8_t kTag[] = {'f', 't', 'y', 'p'};
    const uint8_t* found = std::search(head, head + length, std::begin(kTag), std::end(kTag));
    if (found == head + length || found - head < 4) return 0;
    return (found - head) - 4;
}

// Bytes that live elsewhere: a mapped package or a RAM copy kept alive by `owner`.
class SpanSource : public MediaSource {
   public:
    SpanSource(const uint8_t* data, size_t size, std::shared_ptr<const void> owner, int64_t base)
        : data_(data + base), size_((int64_t)size - base), owner_(std::move(owner)) {}

    int64_t size() const override {
        return size_;
    }
    int64_t readAt(int64_t offset, uint8_t* buffer, int length) const override {
        if (offset < 0) return -1;
        if (offset >= size_) return 0;
        const int64_t count = std::min<int64_t>(length, size_ - offset);
        memcpy(buffer, data_ + offset, (size_t)count);
        return count;
    }

   private:
    const uint8_t* data_;
    int64_t size_;
    std::shared_ptr<const void> owner_;
};

class FileSource : public MediaSource {
   public:
    FileSource(int fd, int64_t base, int64_t size) : fd_(fd), base_(base), size_(size) {}
    ~FileSource() override {
        close(fd_);
    }

    int64_t size() const override {
        return size_;
    }
    int64_t readAt(int64_t offset, uint8_t* buffer, int length) const override {
        if (offset < 0) return -1;
        if (offset >= size_) return 0;
        const int64_t count = std::min<int64_t>(length, size_ - offset);
        return pread(fd_, buffer, (size_t)count, base_ + offset);
    }

   private:
    int fd_;
    int64_t base_;
    int64_t size_;
};

// The picture and the soundtrack open the same file, so they share one RAM copy.
std::shared_ptr<const std::vector<uint8_t>> loadIntoRam(const char* path) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::weak_ptr<const std::vector<uint8_t>>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (auto existing = cache[path].lock()) return existing;
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    if (!vfs::readAll(path, *bytes)) return nullptr;
    cache[path] = bytes;
    return bytes;
}
}  // namespace

void setVideoLoadInRam(bool in_ram) {
    g_load_in_ram = in_ram;
}

std::unique_ptr<MediaSource> openMediaSource(const char* path) {
    if (!path) return nullptr;

    const uint8_t* data = nullptr;
    size_t size = 0;
    if (vfs::find(path, data, size)) {
        const int64_t base = payloadOffset(data, std::min(size, kProbeBytes));
        if ((int64_t)size - base <= 8) return nullptr;
        return std::make_unique<SpanSource>(data, size, nullptr, base);
    }
    if (vfs::isVirtual(path)) return nullptr;

    if (g_load_in_ram) {
        auto bytes = loadIntoRam(path);
        if (!bytes || bytes->size() <= 8) return nullptr;
        const int64_t base = payloadOffset(bytes->data(), std::min(bytes->size(), kProbeBytes));
        return std::make_unique<SpanSource>(bytes->data(), bytes->size(), bytes, base);
    }

    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return nullptr;
    struct stat info = {};
    if (fstat(fd, &info) != 0 || info.st_size <= 8) {
        close(fd);
        return nullptr;
    }
    uint8_t head[kProbeBytes];
    const ssize_t head_size = pread(fd, head, sizeof(head), 0);
    const int64_t base = head_size > 0 ? payloadOffset(head, (size_t)head_size) : 0;
    return std::make_unique<FileSource>(fd, base, (int64_t)info.st_size - base);
}

MediaIo::~MediaIo() {
    close();
}

bool MediaIo::open(std::unique_ptr<MediaSource> source) {
    close();
    if (!source) return false;
    source_ = std::move(source);
    auto* buffer = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
    if (!buffer) {
        close();
        return false;
    }
    io_ = avio_alloc_context(buffer, kIoBufferSize, 0, this, &MediaIo::read, nullptr, &MediaIo::seek);
    if (!io_) {
        av_free(buffer);
        close();
        return false;
    }
    return true;
}

void MediaIo::close() {
    if (io_) {
        // ffmpeg may have swapped the buffer it was given, so free the one it holds now.
        av_freep(&io_->buffer);
        avio_context_free(&io_);
    }
    source_.reset();
    position_ = 0;
}

int MediaIo::read(void* opaque, uint8_t* buffer, int length) {
    auto* self = static_cast<MediaIo*>(opaque);
    const int64_t count = self->source_->readAt(self->position_, buffer, length);
    if (count < 0) return AVERROR(EIO);
    if (count == 0) return AVERROR_EOF;
    self->position_ += count;
    return (int)count;
}

int64_t MediaIo::seek(void* opaque, int64_t offset, int whence) {
    auto* self = static_cast<MediaIo*>(opaque);
    const int64_t size = self->source_->size();
    if (whence == AVSEEK_SIZE) return size;
    const int origin = whence & ~AVSEEK_FORCE;
    const int64_t target = origin == SEEK_SET   ? offset
                           : origin == SEEK_CUR ? self->position_ + offset
                           : origin == SEEK_END ? size + offset
                                                : -1;
    if (target < 0 || target > size) return AVERROR(EINVAL);
    self->position_ = target;
    return target;
}

}  // namespace wallpaper_engine
