#ifndef TEXTURE_DECODE_CACHE_H
#define TEXTURE_DECODE_CACHE_H

#include <memory>
#include <string>

#include "shared/assets/decoded_image.h"

// One cache per owner, so a wallpaper switch never evicts engine textures.
class TextureDecodeCache {
   public:
    TextureDecodeCache();
    ~TextureDecodeCache();
    TextureDecodeCache(const TextureDecodeCache&) = delete;
    TextureDecodeCache& operator=(const TextureDecodeCache&) = delete;

    std::shared_ptr<const wallpaper_engine::DecodedImage> decode(const char* abs_path, int image_index) const;
    void prefetch(const char* abs_path, int image_index);
    // Waits for queued decodes. Intended for wallpaper preparation workers.
    void waitForPrefetches() const;
    bool ready(const char* abs_path, int image_index = 0) const;
    // Drops cache ownership now and destroys decoded buffers off the calling thread.
    void releaseAsync();
    // Drops every entry and trims the allocator.
    void release();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // TEXTURE_DECODE_CACHE_H
