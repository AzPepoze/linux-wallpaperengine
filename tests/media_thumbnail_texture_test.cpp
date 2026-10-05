#include "shared/media/media_thumbnail_texture.h"

#include <map>

#include "test_util.h"

namespace {
std::map<uint32_t, std::vector<uint8_t>> uploads;
uint32_t next_image = 1;
}  // namespace

// Minimal GPU boundary: exercise production history, dirty uploads and resource pruning without a display.
sg_image sg_make_image(const sg_image_desc*) {
    uploads[next_image] = {};
    return {next_image++};
}
sg_resource_state sg_query_image_state(sg_image image) {
    return uploads.count(image.id) ? SG_RESOURCESTATE_VALID : SG_RESOURCESTATE_INVALID;
}
void sg_update_image(sg_image image, const sg_image_data* data) {
    const auto* bytes = (const uint8_t*)data->mip_levels[0].ptr;
    uploads[image.id].assign(bytes, bytes + data->mip_levels[0].size);
}

int main() {
    using namespace wallpaper_engine;
    auto& texture = MediaThumbnailTexture::instance();
    const auto current = texture.create(), previous = texture.create(true);
    texture.flush();
    CHECK(uploads[current.id].size() == 256 * 256 * 4);
    CHECK(uploads[previous.id][3] == 0);
    ThumbnailColors a;
    a.has_thumbnail = true;
    a.width = a.height = 1;
    a.rgba = {255, 0, 0, 255};
    texture.setThumbnail(a);
    texture.flush();
    // With no earlier artwork the previous image mirrors the first one, so the blend's alpha stays opaque.
    CHECK(uploads[current.id][0] == 255 && uploads[previous.id][0] == 255 && uploads[previous.id][3] == 255);
    ThumbnailColors b = a;
    b.rgba = {0, 255, 0, 255};
    texture.setThumbnail(b);
    texture.flush();
    CHECK(uploads[current.id][1] == 255 && uploads[previous.id][0] == 255);
    texture.setThumbnail(b);
    texture.flush();
    CHECK(uploads[previous.id][0] == 255);  // duplicate notifications retain the outgoing artwork
    const auto late = texture.create(true);
    texture.flush();
    CHECK(uploads[late.id][0] == 255);
    texture.setThumbnail({});
    texture.flush();
    CHECK(uploads[current.id][3] == 0 && uploads[previous.id][1] == 255);
    uploads.erase(current.id);
    uploads.erase(previous.id);
    uploads.erase(late.id);
    CHECK(!texture.inUse());
    const auto only_previous = texture.create(true);
    CHECK(texture.inUse());
    uploads.erase(only_previous.id);
    CHECK(!texture.inUse());
    return test::finish("media thumbnail history checks");
}
