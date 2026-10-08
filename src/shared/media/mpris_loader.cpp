#include <memory>
#include <string>

#include "shared/core/plugin.h"
#include "shared/media/media_session.h"

namespace wallpaper_engine {
namespace {
constexpr const char* kMprisPlugin = "mpris";

using CreateSourceFunction = MediaSource* (*)();
using SetThumbnailFunction = void (*)(const char*);

// Kept here so a path set before the plugin loads still reaches it.
std::string g_thumbnail_fallback;

SetThumbnailFunction thumbnailSetter() {
    return reinterpret_cast<SetThumbnailFunction>(pluginSymbol(kMprisPlugin, "lwe_mpris_set_thumbnail_fallback"));
}

// Used whenever the MPRIS plugin is not installed, so the media session simply reports nothing.
class NoopMediaSource : public MediaSource {
   public:
    void start() override {}
    void stop() override {}
    std::vector<MediaEvent> poll() override {
        return {};
    }
};
}  // namespace

// Artwork for $mediaThumbnail when the track has no cover; set by the app before the source starts.
void setMediaThumbnailFallbackImage(const std::string& path) {
    g_thumbnail_fallback = path;
    if (const SetThumbnailFunction set_thumbnail = thumbnailSetter()) set_thumbnail(path.c_str());
}

std::unique_ptr<MediaSource> createMprisMediaSource() {
    const auto create = reinterpret_cast<CreateSourceFunction>(pluginSymbol(kMprisPlugin, "lwe_create_mpris_source"));
    if (!create) return std::make_unique<NoopMediaSource>();
    if (const SetThumbnailFunction set_thumbnail = thumbnailSetter()) set_thumbnail(g_thumbnail_fallback.c_str());
    return std::unique_ptr<MediaSource>(create());
}

}  // namespace wallpaper_engine
