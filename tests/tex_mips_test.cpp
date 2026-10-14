#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "shared/assets/tex_decoder.h"
#include "test_util.h"

namespace {

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back((uint8_t)((value >> shift) & 0xff));
}

void appendText(std::vector<uint8_t>& out, const char* text) {
    out.insert(out.end(), text, text + strlen(text));
}

// One RGBA8 image with a level per entry. Level k is filled with byte value fills[k].
std::vector<uint8_t> makeMippedTex(uint32_t width, uint32_t height, const std::vector<uint8_t>& fills,
                                   const std::vector<std::pair<uint32_t, uint32_t>>& sizes) {
    std::vector<uint8_t> out;
    appendText(out, "TEXV0005");
    out.push_back(0);
    appendText(out, "TEXI0001");
    out.push_back(0);
    appendU32(out, 0);  // format: RGBA8
    appendU32(out, 0);  // flags
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, width);
    appendU32(out, height);
    appendU32(out, 0);
    appendText(out, "TEXB0001");
    out.push_back(0);
    appendU32(out, 1);  // one image
    appendU32(out, (uint32_t)fills.size());
    for (size_t level = 0; level < fills.size(); ++level) {
        const uint32_t level_width = sizes[level].first;
        const uint32_t level_height = sizes[level].second;
        appendU32(out, level_width);
        appendU32(out, level_height);
        appendU32(out, level_width * level_height * 4);
        out.insert(out.end(), level_width * level_height * 4, fills[level]);
    }
    return out;
}

std::string writeTempTex(const std::vector<uint8_t>& bytes) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::string templ = (dir / "tex_mips_XXXXXX").string();
    std::vector<char> buffer(templ.begin(), templ.end());
    buffer.push_back('\0');
    const int fd = mkstemp(buffer.data());
    if (fd < 0) return {};
    const ssize_t written = write(fd, bytes.data(), bytes.size());
    close(fd);
    if (written != (ssize_t)bytes.size()) {
        unlink(buffer.data());
        return {};
    }
    const std::string tex_path = std::string(buffer.data()) + ".tex";
    rename(buffer.data(), tex_path.c_str());
    return tex_path;
}

}  // namespace

int main() {
    // A clean 4x4 chain: 4x4, 2x2, 1x1.
    const std::string clean = writeTempTex(makeMippedTex(4, 4, {200, 100, 50}, {{4, 4}, {2, 2}, {1, 1}}));
    CHECK(!clean.empty());

    const auto metadata = wallpaper_engine::inspectTextureMetadata(clean.c_str());
    CHECK(metadata.valid);
    CHECK(metadata.mip_counts == std::vector<uint32_t>{3});

    const auto levels = wallpaper_engine::decodeTextureMips(clean.c_str(), 0);
    CHECK(levels.size() == 3);
    CHECK(levels.size() == 3 && levels[0].width == 4 && levels[0].height == 4);
    CHECK(levels.size() == 3 && levels[1].width == 2 && levels[1].height == 2);
    CHECK(levels.size() == 3 && levels[2].width == 1 && levels[2].height == 1);
    CHECK(levels.size() == 3 && levels[1].pixels.size() == 2 * 2 * 4);
    CHECK(levels.size() == 3 && levels[1].pixels[0] == 100);
    CHECK(levels.size() == 3 && levels[2].pixels[3] == 50);

    // The plain decode still returns only the full-size level.
    const auto base = wallpaper_engine::decodeTexture(clean.c_str(), 0);
    CHECK(base.valid() && base.width == 4 && base.height == 4);

    // A level of the wrong size makes the whole chain unusable, so nothing is returned.
    const std::string broken = writeTempTex(makeMippedTex(4, 4, {200, 100}, {{4, 4}, {3, 3}}));
    CHECK(!broken.empty());
    CHECK(wallpaper_engine::decodeTextureMips(broken.c_str(), 0).empty());

    // Out-of-range image index and non-.tex paths return nothing.
    CHECK(wallpaper_engine::decodeTextureMips(clean.c_str(), 1).empty());
    CHECK(wallpaper_engine::decodeTextureMips(clean.c_str(), -1).empty());
    CHECK(wallpaper_engine::decodeTextureMips("picture.png", 0).empty());

    unlink(clean.c_str());
    unlink(broken.c_str());
    return test::finish("tex mip levels");
}
