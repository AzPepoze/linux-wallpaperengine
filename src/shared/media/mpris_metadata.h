#ifndef WALLPAPER_ENGINE_MPRIS_METADATA_H
#define WALLPAPER_ENGINE_MPRIS_METADATA_H

namespace wallpaper_engine {

// The subset of the `org.mpris.MediaPlayer2.Player` Metadata dictionary keys the
// engine consumes. Keeping the key -> field mapping pure makes it testable
// without a live D-Bus connection.
enum class MprisMetadataField {
    Ignore,
    Title,
    Artist,
    Album,
    AlbumArtist,
    Genre,
    Length,
    ArtUrl,
};

// Maps an MPRIS Metadata dictionary key to the field it feeds. Unknown keys
// (and null) map to Ignore so callers skip them.
MprisMetadataField classifyMprisMetadataKey(const char* key);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MPRIS_METADATA_H
