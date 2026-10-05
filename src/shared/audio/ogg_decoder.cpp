#include "ogg_decoder.h"

#include <cstdlib>
#include <cstring>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <stb/stb_vorbis.c>
#pragma GCC diagnostic pop

bool looksLikeOgg(const uint8_t* data, size_t size) { return data && size >= 4 && memcmp(data, "OggS", 4) == 0; }

bool decodeOggVorbis(const uint8_t* data, size_t size, std::vector<int16_t>& pcm, uint32_t& channels,
                     uint32_t& sample_rate) {
    if (!looksLikeOgg(data, size) || size > 0x7fffffffu) return false;

    int decoded_channels = 0;
    int decoded_rate = 0;
    short* decoded = nullptr;
    const int frames = stb_vorbis_decode_memory(data, (int)size, &decoded_channels, &decoded_rate, &decoded);
    if (frames <= 0 || !decoded || decoded_channels <= 0 || decoded_rate <= 0) {
        free(decoded);
        return false;
    }

    pcm.assign(decoded, decoded + (size_t)frames * (size_t)decoded_channels);
    free(decoded);
    channels = (uint32_t)decoded_channels;
    sample_rate = (uint32_t)decoded_rate;
    return true;
}
