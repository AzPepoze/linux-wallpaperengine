#include "media_thumbnail_texture.h"

#include <algorithm>

namespace wallpaper_engine {

MediaThumbnailTexture& MediaThumbnailTexture::instance() {
    static MediaThumbnailTexture texture;
    return texture;
}

MediaThumbnailTexture& MediaThumbnailTexture::previous() {
    static MediaThumbnailTexture texture;
    return texture;
}

sg_image MediaThumbnailTexture::create(bool use_previous) {
    if (use_previous) return previous().create();
    sg_image_desc desc = {};
    desc.width = kSize;
    desc.height = kSize;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.stream_update = true;
    const sg_image image = sg_make_image(&desc);
    if (image.id != SG_INVALID_ID) {
        images_.push_back(image.id);
        dirty_.push_back(image.id);
    }
    return image;
}

void MediaThumbnailTexture::setThumbnail(const ThumbnailColors& thumbnail) {
    std::vector<uint8_t> next((size_t)kSize * kSize * 4, 0);
    if (thumbnail.has_thumbnail && thumbnail.width > 0 && thumbnail.height > 0 &&
        thumbnail.rgba.size() >= (size_t)thumbnail.width * thumbnail.height * 4) {
        for (int y = 0; y < kSize; ++y) {
            const int source_y = y * thumbnail.height / kSize;
            for (int x = 0; x < kSize; ++x) {
                const int source_x = x * thumbnail.width / kSize;
                const uint8_t* from = &thumbnail.rgba[((size_t)source_y * thumbnail.width + source_x) * 4];
                std::copy(from, from + 4, &next[((size_t)y * kSize + x) * 4]);
            }
        }
    }
    if (next == pixels_) return;
    // Before the first artwork the previous image is the artwork itself: the blend takes its alpha from the previous
    // image, so an empty or transparent one would leave the first cover fully transparent.
    bool has_artwork = false;
    for (size_t i = 3; i < pixels_.size() && !has_artwork; i += 4) has_artwork = pixels_[i] != 0;
    previous().pixels_ = has_artwork ? pixels_ : next;
    previous().pixels_changed_ = true;
    pixels_ = std::move(next);
    pixels_changed_ = true;
}

void MediaThumbnailTexture::prune() {
    const auto gone = [](uint32_t id) { return sg_query_image_state({id}) != SG_RESOURCESTATE_VALID; };
    images_.erase(std::remove_if(images_.begin(), images_.end(), gone), images_.end());
    dirty_.erase(std::remove_if(dirty_.begin(), dirty_.end(), gone), dirty_.end());
}

bool MediaThumbnailTexture::inUse() {
    prune();
    previous().prune();
    return !images_.empty() || !previous().images_.empty();
}

void MediaThumbnailTexture::flush() {
    if (this != &previous()) previous().flush();
    prune();
    if (pixels_changed_) {
        dirty_ = images_;
        pixels_changed_ = false;
    }
    if (dirty_.empty()) return;
    if (pixels_.empty()) pixels_.assign((size_t)kSize * kSize * 4, 0);
    sg_image_data data = {};
    data.mip_levels[0] = {pixels_.data(), pixels_.size()};
    for (uint32_t id : dirty_) sg_update_image({id}, &data);
    dirty_.clear();
}

}  // namespace wallpaper_engine
