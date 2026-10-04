#ifndef WALLPAPER_ENGINE_FAKE_MEDIA_SOURCE_H
#define WALLPAPER_ENGINE_FAKE_MEDIA_SOURCE_H

#include <mutex>
#include <utility>
#include <vector>

#include "shared/media/media_session.h"

namespace wallpaper_engine {

// Test double: push events and drain them in order.
class FakeMediaSource : public MediaSource {
   public:
    void push(MediaEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
    }

    void start() override {}
    void stop() override {}

    std::vector<MediaEvent> poll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MediaEvent> events;
        events.swap(events_);
        return events;
    }

   private:
    std::mutex mutex_;
    std::vector<MediaEvent> events_;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_FAKE_MEDIA_SOURCE_H
