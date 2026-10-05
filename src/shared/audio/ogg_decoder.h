#ifndef OGG_DECODER_H
#define OGG_DECODER_H

#include <cstddef>
#include <cstdint>
#include <vector>

// miniaudio ships without a Vorbis decoder, so Ogg Vorbis files are decoded to interleaved 16-bit PCM here.
bool decodeOggVorbis(const uint8_t* data, size_t size, std::vector<int16_t>& pcm, uint32_t& channels,
                     uint32_t& sample_rate);

// True when the buffer starts with an Ogg page header.
bool looksLikeOgg(const uint8_t* data, size_t size);

#endif  // OGG_DECODER_H
