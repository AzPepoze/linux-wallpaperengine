#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "decoded_image.h"

// Where a texture actually has visible content. Character layers are often authored on a canvas far larger than the
// art they hold, so effect passes only need to run over this region (plus whatever the pass can displace).
namespace content_bounds {

// Normalised rectangle (0..1, top-left origin) holding every non-transparent texel; invalid when unknown.
struct Rect {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    bool valid = false;
};

struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
};

namespace detail {
constexpr int kAlphaThreshold = 2;

struct Extent {
    uint32_t x0, y0, x1, y1;
    bool any = false;

    Extent(uint32_t w, uint32_t h) : x0(w), y0(h), x1(0), y1(0) {}
    void add(uint32_t x, uint32_t y) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
        any = true;
    }
};

// BC3 stores alpha as two endpoints plus sixteen 3-bit indices. The block is empty when no index reaches a visible
// alpha, which also covers encoders that mark transparency with the six-value mode (index 6 = 0).
inline bool bc3BlockVisible(const uint8_t* block) {
    const int a0 = block[0], a1 = block[1];
    int table[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) table[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i < 5; ++i) table[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        table[6] = 0;
        table[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= (uint64_t)block[2 + i] << (8 * i);
    for (int i = 0; i < 16; ++i) {
        if (table[(bits >> (3 * i)) & 7] > kAlphaThreshold) return true;
    }
    return false;
}

// BC2 stores sixteen explicit 4-bit alpha values in the first eight bytes.
inline bool bc2BlockVisible(const uint8_t* block) {
    for (int i = 0; i < 8; ++i) {
        if ((block[i] & 0x0F) != 0 || (block[i] >> 4) != 0) return true;
    }
    return false;
}
}  // namespace detail

// Bounds of the visible texels. Block-compressed formats are measured per 4x4 block, so the result is conservative.
inline Rect fromImage(const wallpaper_engine::DecodedImage& image) {
    using wallpaper_engine::PixelFormat;
    if (!image.valid()) return {};
    const uint32_t width = image.width, height = image.height;
    detail::Extent extent(width, height);
    uint32_t cell = 1;
    uint32_t cols = width, rows = height;

    if (image.format == PixelFormat::RGBA8) {
        if (image.pixels.size() < (size_t)width * height * 4) return {};
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = &image.pixels[(size_t)y * width * 4];
            for (uint32_t x = 0; x < width; ++x) {
                if (row[x * 4 + 3] > detail::kAlphaThreshold) extent.add(x, y);
            }
        }
    } else if (image.format == PixelFormat::BC2 || image.format == PixelFormat::BC3) {
        cell = 4;
        cols = (width + 3) / 4;
        rows = (height + 3) / 4;
        if (image.pixels.size() < (size_t)cols * rows * 16) return {};
        extent = detail::Extent(cols, rows);
        const bool bc3 = image.format == PixelFormat::BC3;
        for (uint32_t by = 0; by < rows; ++by) {
            for (uint32_t bx = 0; bx < cols; ++bx) {
                const uint8_t* block = &image.pixels[((size_t)by * cols + bx) * 16];
                if (bc3 ? detail::bc3BlockVisible(block) : detail::bc2BlockVisible(block)) extent.add(bx, by);
            }
        }
    } else {
        return {};
    }

    if (!extent.any) return {};
    Rect rect;
    rect.u0 = (float)(extent.x0 * cell) / (float)width;
    rect.v0 = (float)(extent.y0 * cell) / (float)height;
    rect.u1 = std::min(1.0f, (float)((extent.x1 + 1) * cell) / (float)width);
    rect.v1 = std::min(1.0f, (float)((extent.y1 + 1) * cell) / (float)height);
    rect.valid = true;
    return rect;
}

inline Rect expand(Rect rect, float margin) {
    rect.u0 = std::max(0.0f, rect.u0 - margin);
    rect.v0 = std::max(0.0f, rect.v0 - margin);
    rect.u1 = std::min(1.0f, rect.u1 + margin);
    rect.v1 = std::min(1.0f, rect.v1 + margin);
    return rect;
}

// Pixel rectangle covering `rect` on a width x height target, padded so bilinear taps at the edge stay inside.
inline PixelRect toPixels(const Rect& rect, int width, int height, int padding = 2) {
    const int x0 = std::clamp((int)std::floor(rect.u0 * width) - padding, 0, width);
    const int y0 = std::clamp((int)std::floor(rect.v0 * height) - padding, 0, height);
    const int x1 = std::clamp((int)std::ceil(rect.u1 * width) + padding, 0, width);
    const int y1 = std::clamp((int)std::ceil(rect.v1 * height) + padding, 0, height);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// How far (in UV) a pass can move content, for the passes that only resample their input and so keep transparent
// texels transparent. nullopt means the pass may paint outside the content and must not be cropped.
inline std::optional<float> passDisplacement(std::string_view shader_name,
                                             const std::map<std::string, std::vector<float>>& uniforms) {
    auto read = [&uniforms](const char* name) -> std::optional<float> {
        const auto it = uniforms.find(name);
        if (it == uniforms.end() || it->second.empty() || !std::isfinite(it->second.front())) return std::nullopt;
        return std::abs(it->second.front());
    };
    auto ends_with = [shader_name](std::string_view suffix) {
        return shader_name.size() >= suffix.size() &&
               shader_name.compare(shader_name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    // shake: offset <= amp^2 per axis. waterwaves: offset <= strength^2 per axis.
    if (ends_with("shake")) {
        const auto amp = read("g_Amp");
        return amp ? std::optional<float>(*amp * *amp) : std::nullopt;
    }
    if (ends_with("waterwaves")) {
        const auto strength = read("g_Strength");
        return strength ? std::optional<float>(*strength * *strength) : std::nullopt;
    }
    return std::nullopt;
}

}  // namespace content_bounds
