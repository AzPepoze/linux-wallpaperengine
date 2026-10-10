#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "shared/assets/tex_decoder.h"
#include "test_util.h"
using test::check;

namespace {

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back((uint8_t)(value & 0xff));
    out.push_back((uint8_t)((value >> 8) & 0xff));
    out.push_back((uint8_t)((value >> 16) & 0xff));
    out.push_back((uint8_t)((value >> 24) & 0xff));
}

void appendBE32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back((uint8_t)((value >> 24) & 0xff));
    out.push_back((uint8_t)((value >> 16) & 0xff));
    out.push_back((uint8_t)((value >> 8) & 0xff));
    out.push_back((uint8_t)(value & 0xff));
}

void appendText(std::vector<uint8_t>& out, const char* text) {
    out.insert(out.end(), text, text + strlen(text));
}

void appendF32(std::vector<uint8_t>& out, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    appendU32(out, bits);
}

std::vector<uint8_t> makeAtlasTex(bool animated) {
    std::vector<uint8_t> out;
    appendText(out, "TEXV0005");
    out.push_back(0);
    appendText(out, "TEXI0001");
    out.push_back(0);
    appendU32(out, 0);
    appendU32(out, animated ? 4 : 0);
    appendU32(out, 8);
    appendU32(out, 32);
    appendU32(out, 8);
    appendU32(out, 32);
    appendU32(out, 0);
    appendText(out, "TEXB0001");
    out.push_back(0);
    appendU32(out, animated ? 2 : 1);
    for (int i = 0; i < (animated ? 2 : 1); ++i) appendU32(out, 0);  // no mip payload needed for metadata
    if (!animated) return out;
    appendText(out, "TEXS0003");
    out.push_back(0);
    appendU32(out, 3);
    appendU32(out, 8);
    appendU32(out, 32);
    for (int i = 0; i < 3; ++i) {
        appendU32(out, i == 2 ? 1 : 0);
        appendF32(out, i == 1 ? 0.5f : 0.25f);
        appendF32(out, 0);
        appendF32(out, 0);
        appendF32(out, 8);
        appendF32(out, 0);
        appendF32(out, 0);
        appendF32(out, 32);
    }
    return out;
}

std::vector<uint8_t> makeFtypPayload() {
    std::vector<uint8_t> payload;
    appendBE32(payload, 24);
    appendText(payload, "ftyp");
    appendText(payload, "isom");
    appendBE32(payload, 0x00000200);
    appendText(payload, "mp41");
    appendText(payload, "isom");
    return payload;
}

std::vector<uint8_t> makeVideoTex() {
    std::vector<uint8_t> bytes;
    appendText(bytes, "TEXV0005");
    bytes.push_back(0);
    appendText(bytes, "TEXI0001");
    bytes.push_back(0);
    appendU32(bytes, 0);  // format RGBA8
    appendU32(bytes, 0);  // flags
    appendU32(bytes, 8);  // allocated width
    appendU32(bytes, 8);  // allocated height
    appendU32(bytes, 8);  // image width
    appendU32(bytes, 8);  // image height
    appendU32(bytes, 0);  // reserved
    appendText(bytes, "TEXB0001");
    bytes.push_back(0);
    appendU32(bytes, 1);  // image count

    const std::vector<uint8_t> payload = makeFtypPayload();
    appendU32(bytes, 1);  // mip count
    appendU32(bytes, 8);  // mip width
    appendU32(bytes, 8);  // mip height
    appendU32(bytes, (uint32_t)payload.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

std::string writeTempTex(const std::vector<uint8_t>& bytes) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::string path = (dir / "tex_video_detect_XXXXXX").string();
    std::vector<char> tmpl(path.begin(), path.end());
    tmpl.push_back('\0');
    const int fd = mkstemp(tmpl.data());
    if (fd < 0) return {};
    const ssize_t written = write(fd, bytes.data(), bytes.size());
    close(fd);
    if (written != (ssize_t)bytes.size()) {
        unlink(tmpl.data());
        return {};
    }
    const std::string tex_path = std::string(tmpl.data()) + ".tex";
    if (rename(tmpl.data(), tex_path.c_str()) != 0) {
        unlink(tmpl.data());
        return {};
    }
    return tex_path;
}

}  // namespace

int main() {
    for (bool animated : {false, true}) {
        const std::string path = writeTempTex(makeAtlasTex(animated));
        const auto metadata = wallpaper_engine::inspectTextureMetadata(path.c_str());
        check(metadata.valid && metadata.width == 8 && metadata.height == 32, "tall texture dimensions retained");
        if (!animated) {
            check(metadata.animation_frames.empty() && metadata.spritesheet_frames == 0,
                  "static tall texture is not an animation atlas");
        } else {
            check(metadata.animation_frames.size() == 3, "multi-page TEXS timeline retained despite one cell per page");
            const auto* a = wallpaper_engine::textureFrameAtTime(metadata, 0.0f);
            const auto* b = wallpaper_engine::textureFrameAtTime(metadata, 0.25f);
            const auto* c = wallpaper_engine::textureFrameAtTime(metadata, 0.75f);
            check(a && a->image_index == 0 && a->width == 8 && a->height == 32, "first frame rectangle retained");
            check(b && b == &metadata.animation_frames[1], "nonuniform frame durations respected");
            check(c && c->image_index == 1, "timeline advances to second texture page");
            check(wallpaper_engine::textureFrameAtTime(metadata, 1.0f) == a, "timeline loops at total duration");
            check(wallpaper_engine::textureFrameAtTime(metadata, -0.25f) == c, "negative time wraps safely");
        }
        unlink(path.c_str());
    }

    {
        auto bytes = makeAtlasTex(true);
        // A uniform grid can be advertised while one rectangle lies outside it; keep the grid, reject the timeline.
        const size_t frame_start = bytes.size() - 3 * 32;
        for (int i = 0; i < 3; ++i) {
            const float height = 8.0f;
            memcpy(bytes.data() + frame_start + i * 32 + 28, &height, sizeof(height));
        }
        const float invalid_y = 40.0f;
        memcpy(bytes.data() + frame_start + 32 + 12, &invalid_y, sizeof(invalid_y));
        const auto path = writeTempTex(bytes);
        const auto metadata = wallpaper_engine::inspectTextureMetadata(path.c_str());
        check(metadata.animation_frames.empty(), "invalid rectangle timeline rejected");
        check(metadata.spritesheet_cols == 1 && metadata.spritesheet_rows == 4 && metadata.spritesheet_frames == 3,
              "particle grid survives inconsistent rectangle timeline");
        unlink(path.c_str());
    }

    for (float overshoot : {0.004f, 1.0f}) {
        auto bytes = makeAtlasTex(true);
        // Models fractional exporter rounding at an edge.
        const size_t frame_start = bytes.size() - 3 * 32;
        const float width = 8.0f + overshoot;
        memcpy(bytes.data() + frame_start + 16, &width, sizeof(width));
        const auto path = writeTempTex(bytes);
        const auto metadata = wallpaper_engine::inspectTextureMetadata(path.c_str());
        check((!metadata.animation_frames.empty()) == (overshoot < 0.01f),
              "fractional atlas edge rounding accepted, out-of-bounds frame rejected");
        if (!metadata.animation_frames.empty())
            check(metadata.animation_frames[0].width == 8.0f, "edge rounding clipped to texture bounds");
        unlink(path.c_str());
    }
    const std::vector<uint8_t> ftyp = makeFtypPayload();
    check(wallpaper_engine::isVideoContainer(ftyp.data(), ftyp.size()), "ftyp payload detected as video");

    const std::vector<uint8_t> pixels = {0x40, 0x80, 0xc0, 0xff};
    check(!wallpaper_engine::isVideoContainer(pixels.data(), pixels.size()), "pixel payload not detected as video");
    check(!wallpaper_engine::isVideoContainer(nullptr, 0), "empty payload not detected as video");

    const std::string tex_path = writeTempTex(makeVideoTex());
    check(!tex_path.empty(), "temp .tex written");
    if (!tex_path.empty()) {
        const wallpaper_engine::DecodedImage image = wallpaper_engine::decodeTexture(tex_path.c_str(), 0);
        check(image.is_video, "decodeTexture flags .tex with ftyp payload as video");
        check(!image.valid(), "decodeTexture returns no pixels for a video payload");
        unlink(tex_path.c_str());
    }

    return test::finish("tex video detection");
}
