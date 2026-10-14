#include "texture_resize.h"

#include <algorithm>
#include <cmath>

namespace texture_resize {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLanczosRadius = 3.0;

struct Tap {
    int index;
    float weight;
};

// Lanczos3 kernel: sinc(x) * sinc(x / 3) inside |x| < 3, zero outside.
double lanczos(double x) {
    if (x == 0.0) return 1.0;
    if (std::abs(x) >= kLanczosRadius) return 0.0;
    const double px = kPi * x;
    return std::sin(px) / px * std::sin(px / kLanczosRadius) / (px / kLanczosRadius);
}

// Source taps for every destination index along one axis, weights normalised to sum to 1.
std::vector<std::vector<Tap>> buildTaps(int source_size, int target_size, Filter filter) {
    const double scale = static_cast<double>(source_size) / target_size;
    std::vector<std::vector<Tap>> taps(target_size);
    for (int i = 0; i < target_size; ++i) {
        std::vector<Tap>& row = taps[i];
        double total = 0.0;
        if (filter == Filter::Box) {
            // Each destination pixel covers [lo, hi) of the source; weight is the overlap.
            const double lo = i * scale;
            const double hi = (i + 1) * scale;
            for (int s = static_cast<int>(std::floor(lo)); s < std::ceil(hi) && s < source_size; ++s) {
                const double overlap = std::min(hi, s + 1.0) - std::max(lo, static_cast<double>(s));
                if (overlap <= 0.0) continue;
                row.push_back({s, static_cast<float>(overlap)});
                total += overlap;
            }
        } else {
            const double center = (i + 0.5) * scale;
            const double radius = kLanczosRadius * scale;
            const int first = static_cast<int>(std::floor(center - radius));
            const int last = static_cast<int>(std::ceil(center + radius));
            for (int s = first; s <= last; ++s) {
                const double weight = lanczos((s + 0.5 - center) / scale);
                if (weight == 0.0) continue;
                row.push_back({std::clamp(s, 0, source_size - 1), static_cast<float>(weight)});
                total += weight;
            }
        }
        for (Tap& tap : row) tap.weight = static_cast<float>(tap.weight / total);
    }
    return taps;
}

uint8_t toByte(float value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}
}  // namespace

Image resizeRgba8(const uint8_t* source, int source_width, int source_height, int target_width, int target_height,
                  Filter filter) {
    Image result;
    if (!source || source_width <= 0 || source_height <= 0 || target_width <= 0 || target_height <= 0) return result;

    const int out_width = std::min(target_width, source_width);
    const int out_height = std::min(target_height, source_height);
    const std::vector<std::vector<Tap>> x_taps = buildTaps(source_width, out_width, filter);
    const std::vector<std::vector<Tap>> y_taps = buildTaps(source_height, out_height, filter);

    // Horizontal pass into floats so the vertical pass does not round twice.
    std::vector<float> row_buffer(static_cast<size_t>(source_height) * out_width * 4);
    for (int y = 0; y < source_height; ++y) {
        const uint8_t* src_row = source + static_cast<size_t>(y) * source_width * 4;
        float* dst_row = row_buffer.data() + static_cast<size_t>(y) * out_width * 4;
        for (int x = 0; x < out_width; ++x) {
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (const Tap& tap : x_taps[x]) {
                const uint8_t* px = src_row + static_cast<size_t>(tap.index) * 4;
                for (int c = 0; c < 4; ++c) sum[c] += px[c] * tap.weight;
            }
            for (int c = 0; c < 4; ++c) dst_row[x * 4 + c] = sum[c];
        }
    }

    result.width = out_width;
    result.height = out_height;
    result.pixels.resize(static_cast<size_t>(out_width) * out_height * 4);
    for (int y = 0; y < out_height; ++y) {
        uint8_t* dst_row = result.pixels.data() + static_cast<size_t>(y) * out_width * 4;
        for (int x = 0; x < out_width; ++x) {
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (const Tap& tap : y_taps[y]) {
                const float* src_px = row_buffer.data() + (static_cast<size_t>(tap.index) * out_width + x) * 4;
                for (int c = 0; c < 4; ++c) sum[c] += src_px[c] * tap.weight;
            }
            for (int c = 0; c < 4; ++c) dst_row[x * 4 + c] = toByte(sum[c]);
        }
    }
    return result;
}

std::vector<Image> buildMipChain(const uint8_t* source, int width, int height) {
    std::vector<Image> levels;
    if (!source || width <= 0 || height <= 0) return levels;
    int level_width = width;
    int level_height = height;
    const uint8_t* level_pixels = source;
    while (level_width > 1 || level_height > 1) {
        const int next_width = std::max(1, level_width / 2);
        const int next_height = std::max(1, level_height / 2);
        levels.push_back(resizeRgba8(level_pixels, level_width, level_height, next_width, next_height, Filter::Box));
        level_width = next_width;
        level_height = next_height;
        level_pixels = levels.back().pixels.data();
    }
    return levels;
}
}  // namespace texture_resize
