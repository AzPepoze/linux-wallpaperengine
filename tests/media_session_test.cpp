#include "shared/media/media_session.h"

#include <cstdint>
#include <vector>

#include "shared/media/fake_media_source.h"
#include "shared/media/mpris_metadata.h"
#include "shared/media/thumbnail_colors.h"
#include "test_util.h"
using test::expect;

using namespace wallpaper_engine;

namespace {

std::vector<uint8_t> solidImage(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (int i = 0; i < width * height; ++i) {
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }
    return rgba;
}

void testSolidRed() {
    const std::vector<uint8_t> image = solidImage(4, 4, 255, 0, 0);
    const ThumbnailColors colors = extractThumbnailColors(image.data(), 4, 4);
    expect("solid-red", colors.has_thumbnail, "reports a thumbnail");
    expect("solid-red", colors.primary[0] > 0.95f && colors.primary[1] < 0.05f && colors.primary[2] < 0.05f,
           "primary is red");
    expect("solid-red", colors.text[0] > 0.95f && colors.text[1] > 0.95f && colors.text[2] > 0.95f, "text is white");
    expect("solid-red", colors.secondary[0] > 0.95f, "secondary falls back to primary");
    expect("solid-red", colors.tertiary[0] > 0.95f, "tertiary falls back to primary");
}

void testMostlyWhite() {
    const std::vector<uint8_t> image = solidImage(8, 8, 255, 255, 255);
    const ThumbnailColors colors = extractThumbnailColors(image.data(), 8, 8);
    expect("mostly-white", colors.has_thumbnail, "reports a thumbnail");
    expect("mostly-white", colors.primary[0] > 0.95f && colors.primary[1] > 0.95f && colors.primary[2] > 0.95f,
           "primary is white");
    expect("mostly-white", colors.text[0] < 0.05f && colors.text[1] < 0.05f && colors.text[2] < 0.05f, "text is black");
}

void testTransparent() {
    const std::vector<uint8_t> image = solidImage(4, 4, 255, 0, 0, 0);
    const ThumbnailColors colors = extractThumbnailColors(image.data(), 4, 4);
    expect("transparent", !colors.has_thumbnail, "no thumbnail for transparent image");
}

void testTwoColorPrimary() {
    std::vector<uint8_t> image = solidImage(4, 4, 255, 0, 0);
    for (int i = 0; i < 4; ++i) {
        image[i * 4 + 0] = 0;
        image[i * 4 + 1] = 0;
        image[i * 4 + 2] = 255;
    }
    const ThumbnailColors colors = extractThumbnailColors(image.data(), 4, 4);
    expect("two-color", colors.has_thumbnail, "reports a thumbnail");
    expect("two-color", colors.primary[0] > 0.95f && colors.primary[2] < 0.05f, "primary is the larger red area");
}

void testMprisMetadataKeyMapping() {
    using wallpaper_engine::classifyMprisMetadataKey;
    using wallpaper_engine::MprisMetadataField;
    expect("mpris-keys", classifyMprisMetadataKey("xesam:title") == MprisMetadataField::Title, "title");
    expect("mpris-keys", classifyMprisMetadataKey("xesam:artist") == MprisMetadataField::Artist, "artist");
    expect("mpris-keys", classifyMprisMetadataKey("xesam:album") == MprisMetadataField::Album, "album");
    expect("mpris-keys", classifyMprisMetadataKey("xesam:albumArtist") == MprisMetadataField::AlbumArtist, "album artist");
    expect("mpris-keys", classifyMprisMetadataKey("xesam:genre") == MprisMetadataField::Genre, "genre");
    expect("mpris-keys", classifyMprisMetadataKey("mpris:length") == MprisMetadataField::Length, "length");
    // Regression: the art URL was previously dropped, leaving $mediaThumbnail empty.
    expect("mpris-keys", classifyMprisMetadataKey("mpris:artUrl") == MprisMetadataField::ArtUrl, "art url");
    expect("mpris-keys", classifyMprisMetadataKey("xesam:trackNumber") == MprisMetadataField::Ignore, "unknown ignored");
    expect("mpris-keys", classifyMprisMetadataKey(nullptr) == MprisMetadataField::Ignore, "null ignored");
}

void testThumbnailTextureResolution() {
    // The uploaded texture must not be clamped to the coarse palette downsample.
    const std::vector<uint8_t> image = solidImage(64, 64, 10, 20, 30);
    const ThumbnailColors colors = extractThumbnailColors(image.data(), 64, 64);
    expect("thumbnail-res", colors.has_thumbnail, "reports a thumbnail");
    expect("thumbnail-res", colors.width == 64 && colors.height == 64, "keeps resolution up to 256");
}

void testFakeSourceOrdering() {
    FakeMediaSource source;
    MediaEvent first;
    first.kind = MediaEvent::Kind::Status;
    first.enabled = true;
    MediaEvent second;
    second.kind = MediaEvent::Kind::Playback;
    second.state = PlaybackState::Playing;
    source.push(first);
    source.push(second);

    const std::vector<MediaEvent> events = source.poll();
    expect("fake", events.size() == 2, "drains both events");
    expect("fake", events[0].kind == MediaEvent::Kind::Status, "preserves order (first)");
    expect("fake", events[1].kind == MediaEvent::Kind::Playback, "preserves order (second)");
    expect("fake", events[1].state == PlaybackState::Playing, "preserves values");
    expect("fake", source.poll().empty(), "second poll is empty");
}

}  // namespace

int main() {
    testSolidRed();
    testMostlyWhite();
    testTransparent();
    testTwoColorPrimary();
    testThumbnailTextureResolution();
    testMprisMetadataKeyMapping();
    testFakeSourceOrdering();
    return test::finish("media session tests");
}
