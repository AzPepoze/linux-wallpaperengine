// Synthetic checks for .tex video-payload detection. Not part of the default build.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "shared/assets/tex_decoder.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

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

    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("tex video detection checks passed\n");
    return 0;
}
