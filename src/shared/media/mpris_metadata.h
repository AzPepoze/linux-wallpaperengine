#ifndef WALLPAPER_ENGINE_MPRIS_METADATA_H
#define WALLPAPER_ENGINE_MPRIS_METADATA_H

namespace wallpaper_engine {

// The MPRIS Metadata keys the engine uses; a pure mapping, testable without D-Bus.
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

// Maps a Metadata key to its field; unknown and null keys map to Ignore.
MprisMetadataField classifyMprisMetadataKey(const char* key);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MPRIS_METADATA_H
