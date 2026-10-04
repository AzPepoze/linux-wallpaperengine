#ifndef TEXTURE_DECODE_CACHE_H
#define TEXTURE_DECODE_CACHE_H

#include <memory>

#include "shared/assets/decoded_image.h"

// Caches decoded textures keyed by absolute path and image index, deduplicating
// concurrent decodes with a shared future. One instance per owner (a process-wide
// engine cache and a per-wallpaper cache) so a wallpaper switch never evicts
// engine/internal textures.
class TextureDecodeCache {
   public:
    TextureDecodeCache();
    ~TextureDecodeCache();
    TextureDecodeCache(const TextureDecodeCache&) = delete;
    TextureDecodeCache& operator=(const TextureDecodeCache&) = delete;

    // Decodes (or returns the cached) image for `abs_path` and `image_index`.
    std::shared_ptr<const wallpaper_engine::DecodedImage> decode(const char* abs_path, int image_index) const;
    // Enqueues a decode on the TaskPool and records it under the same key.
    void prefetch(const char* abs_path, int image_index);
    // Drops every entry and trims the allocator.
    void release();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif  // TEXTURE_DECODE_CACHE_H
