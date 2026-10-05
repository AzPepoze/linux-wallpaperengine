#include "shared/audio/ogg_decoder.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "test_util.h"

int main() {
    std::ifstream file("tests/data/tone_8khz_mono.ogg", std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty());
    CHECK(looksLikeOgg(bytes.data(), bytes.size()));

    std::vector<int16_t> pcm;
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    CHECK(decodeOggVorbis(bytes.data(), bytes.size(), pcm, channels, sample_rate));
    CHECK(channels == 1);
    CHECK(sample_rate == 8000);
    // 0.1 s at 8 kHz; the encoder may pad by a block.
    CHECK(pcm.size() >= 800 && pcm.size() < 2400);

    int peak = 0;
    for (int16_t sample : pcm) peak = std::max(peak, std::abs((int)sample));
    CHECK(peak > 1000);  // an actual tone, not silence

    // Truncated and non-Ogg input must fail cleanly.
    std::vector<int16_t> unused;
    CHECK(!decodeOggVorbis(bytes.data(), 64, unused, channels, sample_rate));
    const uint8_t garbage[16] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!looksLikeOgg(garbage, sizeof(garbage)));
    CHECK(!decodeOggVorbis(garbage, sizeof(garbage), unused, channels, sample_rate));
    CHECK(!decodeOggVorbis(nullptr, 0, unused, channels, sample_rate));

    return test::finish("ogg decoder tests");
}
