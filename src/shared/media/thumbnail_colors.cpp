#include "shared/media/thumbnail_colors.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace wallpaper_engine {
namespace {

constexpr int kMaxDimension = 32;
constexpr std::size_t kClusters = 4;
constexpr uint8_t kOpaqueAlpha = 128;

struct Pixel {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct Cluster {
    float rgb[3]{};
    std::size_t count = 0;
};

float channel(const Pixel& pixel, int index) {
    if (index == 0) return pixel.r;
    if (index == 1) return pixel.g;
    return pixel.b;
}

float srgbToLinear(float value) {
    return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float relativeLuminance(const float rgb[3]) {
    return 0.2126f * srgbToLinear(rgb[0]) + 0.7152f * srgbToLinear(rgb[1]) + 0.0722f * srgbToLinear(rgb[2]);
}

float contrastRatio(float a, float b) {
    const float lighter = std::max(a, b);
    const float darker = std::min(a, b);
    return (lighter + 0.05f) / (darker + 0.05f);
}

void copyColor(float destination[3], const float source[3]) {
    destination[0] = source[0];
    destination[1] = source[1];
    destination[2] = source[2];
}

std::vector<uint8_t> downsample(const uint8_t* rgba, int width, int height, int& out_width, int& out_height) {
    out_width = std::min(width, kMaxDimension);
    out_height = std::min(height, kMaxDimension);
    std::vector<uint8_t> out(static_cast<std::size_t>(out_width) * out_height * 4);
    for (int y = 0; y < out_height; ++y) {
        const int y0 = y * height / out_height;
        const int y1 = std::max(y0 + 1, (y + 1) * height / out_height);
        for (int x = 0; x < out_width; ++x) {
            const int x0 = x * width / out_width;
            const int x1 = std::max(x0 + 1, (x + 1) * width / out_width);
            uint32_t sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* pixel = rgba + (static_cast<std::size_t>(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += pixel[c];
                    ++count;
                }
            }
            uint8_t* pixel = out.data() + (static_cast<std::size_t>(y) * out_width + x) * 4;
            for (int c = 0; c < 4; ++c) pixel[c] = static_cast<uint8_t>(sum[c] / static_cast<uint32_t>(count));
        }
    }
    return out;
}

std::vector<Pixel> opaquePixels(const std::vector<uint8_t>& rgba, int width, int height) {
    std::vector<Pixel> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * height);
    for (int i = 0; i < width * height; ++i) {
        const uint8_t* p = rgba.data() + static_cast<std::size_t>(i) * 4;
        if (p[3] < kOpaqueAlpha) continue;
        Pixel pixel;
        pixel.r = p[0] / 255.0f;
        pixel.g = p[1] / 255.0f;
        pixel.b = p[2] / 255.0f;
        pixels.push_back(pixel);
    }
    return pixels;
}

std::vector<std::vector<Pixel>> medianCut(std::vector<Pixel> pixels) {
    std::vector<std::vector<Pixel>> boxes;
    boxes.push_back(std::move(pixels));

    while (boxes.size() < kClusters) {
        std::size_t target = boxes.size();
        float best_range = 0.0f;
        int best_channel = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].size() < 2) continue;
            for (int c = 0; c < 3; ++c) {
                float low = 1.0f;
                float high = 0.0f;
                for (const Pixel& pixel : boxes[i]) {
                    const float value = channel(pixel, c);
                    low = std::min(low, value);
                    high = std::max(high, value);
                }
                if (high - low > best_range) {
                    best_range = high - low;
                    best_channel = c;
                    target = i;
                }
            }
        }
        if (target == boxes.size()) break;

        std::vector<Pixel>& box = boxes[target];
        std::sort(box.begin(), box.end(), [best_channel](const Pixel& a, const Pixel& b) {
            return channel(a, best_channel) < channel(b, best_channel);
        });
        std::size_t split = box.size() / 2;
        float largest_gap = 0.0f;
        for (std::size_t i = 1; i < box.size(); ++i) {
            const float gap = channel(box[i], best_channel) - channel(box[i - 1], best_channel);
            if (gap > largest_gap) {
                largest_gap = gap;
                split = i;
            }
        }
        if (largest_gap <= 0.0f) break;

        std::vector<Pixel> right(box.begin() + static_cast<std::ptrdiff_t>(split), box.end());
        box.erase(box.begin() + static_cast<std::ptrdiff_t>(split), box.end());
        boxes.push_back(std::move(right));
    }
    return boxes;
}

std::vector<Cluster> clusterAverages(const std::vector<std::vector<Pixel>>& boxes) {
    std::vector<Cluster> clusters;
    for (const std::vector<Pixel>& box : boxes) {
        if (box.empty()) continue;
        float sum[3] = {0.0f, 0.0f, 0.0f};
        for (const Pixel& pixel : box) {
            sum[0] += pixel.r;
            sum[1] += pixel.g;
            sum[2] += pixel.b;
        }
        Cluster cluster;
        cluster.count = box.size();
        for (int c = 0; c < 3; ++c) cluster.rgb[c] = sum[c] / static_cast<float>(box.size());
        clusters.push_back(cluster);
    }
    std::sort(clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) { return a.count > b.count; });
    return clusters;
}

}  // namespace

ThumbnailColors extractThumbnailColors(const uint8_t* rgba, int width, int height) {
    ThumbnailColors result;
    if (!rgba || width <= 0 || height <= 0) return result;

    int out_width = 0;
    int out_height = 0;
    std::vector<uint8_t> small = downsample(rgba, width, height, out_width, out_height);

    std::vector<Cluster> clusters = clusterAverages(medianCut(opaquePixels(small, out_width, out_height)));
    if (clusters.empty()) return result;

    copyColor(result.primary, clusters[0].rgb);
    copyColor(result.secondary, clusters.size() > 1 ? clusters[1].rgb : clusters[0].rgb);
    copyColor(result.tertiary, clusters.size() > 2 ? clusters[2].rgb : clusters[0].rgb);

    const float luminance = relativeLuminance(result.primary);
    if (luminance > 0.5f) {
        result.text[0] = result.text[1] = result.text[2] = 0.0f;
    } else {
        result.text[0] = result.text[1] = result.text[2] = 1.0f;
    }

    const bool white_contrasts_more = contrastRatio(luminance, 1.0f) >= contrastRatio(luminance, 0.0f);
    result.high_contrast[0] = result.high_contrast[1] = result.high_contrast[2] = white_contrasts_more ? 1.0f : 0.0f;

    result.has_thumbnail = true;
    result.rgba = std::move(small);
    result.width = out_width;
    result.height = out_height;
    return result;
}

}  // namespace wallpaper_engine
