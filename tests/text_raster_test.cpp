#include "wallpaper/2d/layers/text/text_raster.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "shared/core/vfs.h"
#include "test_util.h"

namespace {

const char* const kFontCandidates[] = {
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
};

// Optional first argument overrides the font; otherwise the first installed candidate is used.
std::string pickFont(int argc, char** argv) {
    if (argc > 1) return argv[1];
    for (const char* candidate : kFontCandidates)
        if (vfs::exists(candidate)) return candidate;
    return "";
}

uint64_t fnv1a(const std::vector<float>& pixels) {
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(pixels.data());
    for (size_t i = 0; i < pixels.size() * sizeof(float); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

TextRasterRequest makeRequest(const std::string& font_path, const std::string& text) {
    TextRasterRequest request;
    request.font_path = font_path;
    request.config.name = "test";
    request.config.text = text;
    request.config.font = font_path;
    request.config.pointsize = 24.0f;
    request.config.size = {300.0f, 60.0f};
    return request;
}

bool sameResult(const TextRasterResult& a, const TextRasterResult& b) {
    return a.width == b.width && a.height == b.height && a.pixel_scale == b.pixel_scale && a.pixels == b.pixels &&
           a.size[0] == b.size[0] && a.size[1] == b.size[1];
}

void report(const char* label, const TextRasterResult& result) {
    std::printf("%s: width=%d height=%d pixel_scale=%.3f fnv1a=%016" PRIx64 "\n", label, result.width, result.height,
                result.pixel_scale, fnv1a(result.pixels));
}

// Placing the crop back at its offset must reproduce the uncropped raster exactly.
void checkCrop(const char* label, const TextRasterRequest& request) {
    TextRasterResult full;
    CHECK(rasterizeText(request, full));
    TextRasterResult cropped = full;
    cropToContent(cropped);
    std::printf("%s crop: cropped=%d %dx%d of %dx%d at %d,%d\n", label, cropped.cropped ? 1 : 0, cropped.width,
                cropped.height, cropped.full_width, cropped.full_height, cropped.content_x, cropped.content_y);
    CHECK(cropped.full_width == full.width && cropped.full_height == full.height);
    CHECK(cropped.size[0] == full.size[0] && cropped.size[1] == full.size[1]);
    if (!cropped.cropped) {
        CHECK(cropped.pixels == full.pixels);
        return;
    }
    CHECK(cropped.content_x + cropped.width <= full.width);
    CHECK(cropped.content_y + cropped.height <= full.height);
    CHECK(cropped.pixels.size() == (size_t)cropped.width * cropped.height * 4);

    std::vector<float> rebuilt((size_t)full.width * full.height * 4, 0.0f);
    for (int y = 0; y < cropped.height; ++y) {
        const float* source = &cropped.pixels[(size_t)y * cropped.width * 4];
        float* target = &rebuilt[((size_t)(cropped.content_y + y) * full.width + cropped.content_x) * 4];
        std::copy(source, source + (size_t)cropped.width * 4, target);
    }
    CHECK(rebuilt == full.pixels);
}

void checkRaster(const char* label, const TextRasterRequest& request) {
    TextRasterResult result;
    CHECK(rasterizeText(request, result));
    CHECK(!result.pixels.empty());
    CHECK(result.width > 0);
    CHECK(result.height > 0);
    CHECK(result.pixels.size() == (size_t)result.width * (size_t)result.height * 4);
    report(label, result);
}

// Four threads share one font repository and must produce identical output.
void checkConcurrentRaster(const TextRasterRequest& request) {
    constexpr int kThreads = 4;
    TextRasterResult results[kThreads];
    bool ok[kThreads] = {};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i)
        threads.emplace_back([&, i] { ok[i] = rasterizeText(request, results[i]); });
    for (auto& thread : threads) thread.join();

    for (int i = 0; i < kThreads; ++i) {
        CHECK(ok[i]);
        CHECK(sameResult(results[0], results[i]));
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string font_path = pickFont(argc, argv);
    if (font_path.empty() || !vfs::exists(font_path.c_str())) {
        std::fprintf(stderr, "no readable font; pass a .ttf path as the first argument\n");
        return 1;
    }

    const TextRasterRequest plain = makeRequest(font_path, "Hello World");
    checkRaster("plain", plain);
    checkCrop("plain", plain);
    checkConcurrentRaster(plain);

    TextRasterRequest wrapped = makeRequest(font_path, "The quick brown fox jumps over the lazy dog.\nSecond line.");
    wrapped.config.pointsize = 16.0f;
    wrapped.config.size = {0.0f, 0.0f};
    wrapped.config.limit_width = true;
    wrapped.config.maxwidth = 200.0f;
    checkRaster("wrapped", wrapped);
    checkCrop("wrapped", wrapped);

    TextRasterRequest opaque = makeRequest(font_path, "Opaque");
    opaque.config.opaque_background = true;
    opaque.config.background_color = {0.1f, 0.2f, 0.3f};
    opaque.config.brightness = 1.5f;
    opaque.config.padding = 4.0f;
    opaque.tint[0] = 0.5f;
    opaque.tint[1] = 1.0f;
    opaque.tint[2] = 0.25f;
    checkRaster("opaque", opaque);
    checkCrop("opaque", opaque);
    checkConcurrentRaster(opaque);

    return test::finish("text raster tests");
}
