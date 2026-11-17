#include "capture/image_writer.h"

// Static so this file does not clash with the stb implementation in diagnostic_export.cpp.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <utility>

#include "shared/core/logger.h"

namespace capture {

namespace {

void writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
    const int stride = width * 4;
    if (!stbi_write_png(path.c_str(), width, height, 4, rgba.data(), stride)) {
        LOG_E("capture: failed to write %s", path.c_str());
    }
}

}  // namespace

ImageWriter::ImageWriter() : worker_(&ImageWriter::runWorker, this) {}

ImageWriter::~ImageWriter() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    has_work_.notify_all();
    worker_.join();
}

void ImageWriter::submit(std::string path, int width, int height, std::vector<uint8_t> rgba) {
    std::unique_lock<std::mutex> lock(mutex_);
    has_room_.wait(lock, [this] { return queue_.size() < kMaxPendingImages; });
    queue_.push_back(PendingImage{std::move(path), width, height, std::move(rgba)});
    lock.unlock();
    has_work_.notify_one();
}

void ImageWriter::drain() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !writing_; });
}

void ImageWriter::runWorker() {
    for (;;) {
        PendingImage image;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            has_work_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) return;
            image = std::move(queue_.front());
            queue_.pop_front();
            writing_ = true;
        }
        has_room_.notify_one();

        writePng(image.path, image.width, image.height, image.rgba);

        std::lock_guard<std::mutex> lock(mutex_);
        writing_ = false;
        if (queue_.empty()) idle_.notify_all();
    }
}

}  // namespace capture
