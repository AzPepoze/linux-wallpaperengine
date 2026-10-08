#include "text_texture_cache.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

#include "shared/core/task_pool.h"

namespace {

// Allows two rasterizations at once, so text does not take over the worker pool.
class RasterGate {
   public:
    void enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        slot_.wait(lock, [this] { return active_ < kMaxConcurrent; });
        ++active_;
    }

    void leave() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
        slot_.notify_one();
    }

   private:
    static constexpr int kMaxConcurrent = 2;
    std::mutex mutex_;
    std::condition_variable slot_;
    int active_ = 0;
};

RasterGate& rasterGate() {
    static RasterGate gate;
    return gate;
}

// Half precision holds every value a text texture needs (HDR brightness stays below its maximum).
uint16_t toHalf(float value) {
    const _Float16 half = static_cast<_Float16>(std::clamp(value, 0.0f, 65504.0f));
    uint16_t bits = 0;
    static_assert(sizeof(half) == sizeof(bits), "half must be 16 bits");
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

RasterOutput rasterInBackground(TextRasterRequest request, bool crop) {
    RasterOutput output;
    rasterGate().enter();
    output.ok = rasterizeText(request, output.result);
    if (output.ok && crop) cropToContent(output.result);
    if (output.ok) {
        output.texels.resize(output.result.pixels.size());
        std::transform(output.result.pixels.begin(), output.result.pixels.end(), output.texels.begin(), toHalf);
        std::vector<float>().swap(output.result.pixels);
    }
    rasterGate().leave();
    return output;
}

}  // namespace

std::shared_ptr<TextTexture> TextTextureCache::find(const std::string& key) {
    auto found = textures_.find(key);
    return found == textures_.end() ? nullptr : found->second.lock();
}

void TextTextureCache::remember(const std::string& key, const std::shared_ptr<TextTexture>& texture) {
    for (auto entry = textures_.begin(); entry != textures_.end();)
        entry = entry->second.expired() ? textures_.erase(entry) : std::next(entry);
    textures_[key] = texture;
}

std::shared_future<RasterOutput> TextTextureCache::rasterFor(const std::string& key, const TextRasterRequest& request,
                                                             bool crop) {
    purgeFinishedRasters();
    auto flight = flights_.find(key);
    if (flight != flights_.end()) return flight->second;
    std::shared_future<RasterOutput> future = TaskPool::instance().enqueue(rasterInBackground, request, crop).share();
    flights_[key] = future;
    return future;
}

// A finished raster stays alive for every layer still holding its future; only the lookup entry goes.
void TextTextureCache::purgeFinishedRasters() {
    for (auto flight = flights_.begin(); flight != flights_.end();) {
        if (flight->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            flight = flights_.erase(flight);
        else
            ++flight;
    }
}
