#ifndef IMAGE_WRITER_H
#define IMAGE_WRITER_H

#include <stdint.h>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace capture {

// Writes PNG files on a worker thread so the render loop does not wait for disk.
class ImageWriter {
   public:
    ImageWriter();
    ~ImageWriter();

    // Blocks while the queue is full.
    void submit(std::string path, int width, int height, std::vector<uint8_t> rgba);
    // Blocks until every submitted image is written. The worker keeps running.
    void drain();

   private:
    struct PendingImage {
        std::string path;
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    static constexpr size_t kMaxPendingImages = 8;

    void runWorker();

    std::mutex mutex_;
    std::condition_variable has_work_;
    std::condition_variable has_room_;
    std::condition_variable idle_;
    std::deque<PendingImage> queue_;
    bool writing_ = false;
    bool stop_ = false;
    std::thread worker_;
};

}  // namespace capture

#endif  // IMAGE_WRITER_H
