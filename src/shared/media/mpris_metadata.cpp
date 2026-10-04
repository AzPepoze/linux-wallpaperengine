#include "shared/media/mpris_metadata.h"

#include <cstring>

namespace wallpaper_engine {

MprisMetadataField classifyMprisMetadataKey(const char* key) {
    if (!key) return MprisMetadataField::Ignore;
    if (std::strcmp(key, "xesam:title") == 0) return MprisMetadataField::Title;
    if (std::strcmp(key, "xesam:artist") == 0) return MprisMetadataField::Artist;
    if (std::strcmp(key, "xesam:album") == 0) return MprisMetadataField::Album;
    if (std::strcmp(key, "xesam:albumArtist") == 0) return MprisMetadataField::AlbumArtist;
    if (std::strcmp(key, "xesam:genre") == 0) return MprisMetadataField::Genre;
    if (std::strcmp(key, "mpris:length") == 0) return MprisMetadataField::Length;
    if (std::strcmp(key, "mpris:artUrl") == 0) return MprisMetadataField::ArtUrl;
    return MprisMetadataField::Ignore;
}

}  // namespace wallpaper_engine
