#include "shared/assets/texture_decode_cache.h"

#include <malloc.h>

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "shared/assets/tex_decoder.h"
#include "shared/core/task_pool.h"
#include "shared/core/vfs.h"

struct TextureDecodeCache::Impl {
    using Entry = std::shared_future<std::shared_ptr<const wallpaper_engine::DecodedImage>>;

    std::mutex mutex;
    std::unordered_map<std::string, Entry> entries;

    static std::string key(const char* path, int image_index) {
        return std::string(path) + "#" + std::to_string(image_index);
    }
};

TextureDecodeCache::TextureDecodeCache() : impl_(std::make_unique<Impl>()) {}

TextureDecodeCache::~TextureDecodeCache() = default;

std::shared_ptr<const wallpaper_engine::DecodedImage> TextureDecodeCache::decode(const char* abs_path,
                                                                                 int image_index) const {
    const std::string key = Impl::key(abs_path, image_index);
    Impl::Entry pending;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto found = impl_->entries.find(key);
        if (found != impl_->entries.end()) pending = found->second;
    }
    if (pending.valid()) return pending.get();

    auto image =
        std::make_shared<const wallpaper_engine::DecodedImage>(wallpaper_engine::decodeTexture(abs_path, image_index));
    std::promise<std::shared_ptr<const wallpaper_engine::DecodedImage>> ready;
    ready.set_value(image);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.emplace(key, ready.get_future().share());
    return image;
}

void TextureDecodeCache::prefetch(const char* abs_path, int image_index) {
    const std::string path = abs_path;
    const std::string key = Impl::key(abs_path, image_index);
    const vfs::PackageHandle package = vfs::currentPackage();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->entries.find(key) != impl_->entries.end()) return;
    auto task = TaskPool::instance().enqueue([path, image_index, package] {
        vfs::ScopedBinding binding(package);
        return std::make_shared<const wallpaper_engine::DecodedImage>(
            wallpaper_engine::decodeTexture(path.c_str(), image_index));
    });
    impl_->entries.emplace(key, task.share());
}

void TextureDecodeCache::waitForPrefetches() const {
    std::vector<Impl::Entry> pending;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        pending.reserve(impl_->entries.size());
        for (const auto& entry : impl_->entries) pending.push_back(entry.second);
    }
    for (const auto& future : pending) {
        if (future.valid()) future.wait();
    }
}

bool TextureDecodeCache::ready(const char* abs_path, int image_index) const {
    if (!abs_path) return false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto found = impl_->entries.find(Impl::key(abs_path, image_index));
        if (found != impl_->entries.end())
            return found->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    const_cast<TextureDecodeCache*>(this)->prefetch(abs_path, image_index);
    return false;
}

void TextureDecodeCache::release() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->entries.clear();
    }
    malloc_trim(0);
}

void TextureDecodeCache::releaseAsync() {
    std::unordered_map<std::string, Impl::Entry> retired;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        retired.swap(impl_->entries);
    }
    if (retired.empty()) return;
    try {
        TaskPool::instance().enqueue([retired = std::move(retired)]() mutable {
            retired.clear();
            malloc_trim(0);
        });
    } catch (...) {
        retired.clear();
        malloc_trim(0);
    }
}
