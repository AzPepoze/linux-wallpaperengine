// Checks that video bytes are located and served the same way from disk and from RAM.

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "shared/assets/media/media_source.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void expect(const char* test, bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        std::printf("FAIL %s: %s\n", test, what);
        ++g_failures;
    }
}

// A fake MP4: a box header, the "ftyp" tag, then payload bytes.
std::vector<uint8_t> mp4Bytes() {
    std::vector<uint8_t> bytes = {0, 0, 0, 24, 'f', 't', 'y', 'p'};
    for (int i = 0; i < 100; ++i) bytes.push_back((uint8_t)i);
    return bytes;
}

std::string writeFile(const std::vector<uint8_t>& prefix, const std::vector<uint8_t>& payload) {
    char path[] = "/tmp/lwe_media_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) return "";
    write(fd, prefix.data(), prefix.size());
    write(fd, payload.data(), payload.size());
    close(fd);
    return path;
}

void checkSource(const char* test, const std::string& path, const std::vector<uint8_t>& payload) {
    auto source = wallpaper_engine::openMediaSource(path.c_str());
    expect(test, source != nullptr, "opens the file");
    if (!source) return;
    expect(test, source->size() == (int64_t)payload.size(), "size excludes the TEX header");

    uint8_t buffer[16] = {};
    expect(test, source->readAt(0, buffer, 8) == 8 && memcmp(buffer, payload.data(), 8) == 0,
           "reads start at the MP4 box");
    expect(test, source->readAt(50, buffer, 16) == 16 && buffer[0] == payload[50], "reads from the middle");
    expect(test, source->readAt((int64_t)payload.size() - 4, buffer, 16) == 4, "clamps a read at the end");
    expect(test, source->readAt((int64_t)payload.size(), buffer, 16) == 0, "reports end of data");
}

void testEmbeddedAndPlain() {
    const std::vector<uint8_t> payload = mp4Bytes();
    const std::vector<uint8_t> tex_header(300, 0x7f);

    const std::string embedded = writeFile(tex_header, payload);
    const std::string plain = writeFile({}, payload);

    for (bool in_ram : {false, true}) {
        wallpaper_engine::setVideoLoadInRam(in_ram);
        checkSource(in_ram ? "embedded/ram" : "embedded/disk", embedded, payload);
        checkSource(in_ram ? "plain/ram" : "plain/disk", plain, payload);
    }
    wallpaper_engine::setVideoLoadInRam(false);
    unlink(embedded.c_str());
    unlink(plain.c_str());
}

void testMissingFile() {
    expect("missing", wallpaper_engine::openMediaSource("/tmp/lwe_does_not_exist.mp4") == nullptr,
           "a missing file yields no source");
    expect("missing", wallpaper_engine::openMediaSource(nullptr) == nullptr, "a null path yields no source");
}

}  // namespace

int main() {
    testEmbeddedAndPlain();
    testMissingFile();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
