#include "shared/assets/texture_decode_cache.h"

#include <malloc.h>

#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

#include "shared/assets/tex_decoder.h"
#include "shared/core/task_pool.h"

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
    auto task = TaskPool::instance().enqueue([path, image_index] {
        return std::make_shared<const wallpaper_engine::DecodedImage>(
            wallpaper_engine::decodeTexture(path.c_str(), image_index));
    });
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.emplace(key, task.share());
}

void TextureDecodeCache::release() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->entries.clear();
    }
    malloc_trim(0);
}
