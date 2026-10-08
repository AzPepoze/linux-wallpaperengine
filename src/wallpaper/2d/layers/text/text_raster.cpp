#include "text_raster.h"

#include <math.h>
#include <stb/stb_truetype.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#include "font_repository.h"

namespace {

// Wallpaper Engine point sizes are authored in design units at a fixed ratio.
constexpr float kDesignUnitsPerPoint = 4.0f;
// Supersampling factor of the generated glyph texture for crisp text.
constexpr float kMaxTexturePixelScale = 2.0f;
constexpr int kMaxTextureSize = 4096;

// The authored font at one em size, with per-character fallback to system fonts.
struct TextFont {
    const stbtt_fontinfo* primary = nullptr;
    float px = 0.0f;
    float scale = 0.0f;

    struct Pick {
        const stbtt_fontinfo* info;
        float scale;
    };

    Pick pick(int codepoint) const {
        if (codepoint < 0x20 || stbtt_FindGlyphIndex(primary, codepoint) != 0) return {primary, scale};
        // The repository keeps fonts for the life of the process, so the info pointer outlives this call.
        if (std::shared_ptr<const LoadedFont> fallback = fontRepository().fallback(codepoint))
            if (stbtt_FindGlyphIndex(&fallback->info, codepoint) != 0)
                return {&fallback->info, stbtt_ScaleForMappingEmToPixels(&fallback->info, px)};
        return {primary, scale};
    }
};

// Advance of one character in pixels, including the kerning pair with the next character in the same font.
float advanceOf(const TextFont& font, int codepoint, int next_codepoint) {
    const TextFont::Pick glyph = font.pick(codepoint);
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(glyph.info, codepoint, &advance, &bearing);
    float width = advance * glyph.scale;
    if (next_codepoint > 0 && font.pick(next_codepoint).info == glyph.info)
        width += stbtt_GetCodepointKernAdvance(glyph.info, codepoint, next_codepoint) * glyph.scale;
    return width;
}

int nextCodepoint(const std::string& text, size_t& index) {
    const unsigned char c = (unsigned char)text[index];
    if (c < 0x80) {
        index += 1;
        return c;
    }
    if ((c >> 5) == 0x6 && index + 1 < text.size()) {
        const int cp = ((c & 0x1F) << 6) | (text[index + 1] & 0x3F);
        index += 2;
        return cp;
    }
    if ((c >> 4) == 0xE && index + 2 < text.size()) {
        const int cp = ((c & 0x0F) << 12) | ((text[index + 1] & 0x3F) << 6) | (text[index + 2] & 0x3F);
        index += 3;
        return cp;
    }
    if ((c >> 3) == 0x1E && index + 3 < text.size()) {
        const int cp = ((c & 0x07) << 18) | ((text[index + 1] & 0x3F) << 12) | ((text[index + 2] & 0x3F) << 6) |
                       (text[index + 3] & 0x3F);
        index += 4;
        return cp;
    }
    index += 1;
    return c;
}

float measureText(const TextFont& font, const std::string& text) {
    float width = 0.0f;
    size_t index = 0;
    while (index < text.size()) {
        const int cp = nextCodepoint(text, index);
        size_t peek = index;
        const int next = peek < text.size() ? nextCodepoint(text, peek) : 0;
        width += advanceOf(font, cp, next);
    }
    return width;
}

std::vector<std::string> wrapWords(const TextFont& font, const std::string& paragraph, float max_width) {
    std::vector<std::string> lines;
    std::string line;
    size_t index = 0;
    while (index <= paragraph.size()) {
        const size_t space = paragraph.find(' ', index);
        const std::string word =
            paragraph.substr(index, space == std::string::npos ? std::string::npos : space - index);
        if (!word.empty()) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (line.empty() || measureText(font, candidate) <= max_width + 0.5f) {
                line = candidate;
            } else {
                lines.push_back(line);
                line = word;
            }
        }
        if (space == std::string::npos) break;
        index = space + 1;
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

std::vector<std::string> layoutLines(const TextFont& font, const std::string& text, float max_width) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        std::string paragraph = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!paragraph.empty() && paragraph.back() == '\r') paragraph.pop_back();
        for (auto& line : wrapWords(font, paragraph, max_width)) lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

// Floating-point pixels retain HDR brightness. The layer tint colors the glyphs; opaque
// backgrounds blend glyph coverage into their background color.
void blitLine(std::vector<float>& pixels, int canvas_w, int canvas_h, const TextFont& font, const std::string& line,
              float pen_x, float baseline, bool opaque_background, float brightness) {
    size_t index = 0;
    while (index < line.size()) {
        const int cp = nextCodepoint(line, index);
        size_t peek = index;
        const int next = peek < line.size() ? nextCodepoint(line, peek) : 0;
        const TextFont::Pick glyph = font.pick(cp);

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(glyph.info, cp, glyph.scale, glyph.scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0;
        const int gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<uint8_t> bitmap((size_t)gw * (size_t)gh);
            stbtt_MakeCodepointBitmap(glyph.info, bitmap.data(), gw, gh, gw, glyph.scale, glyph.scale, cp);
            const int gx = (int)lroundf(pen_x) + x0;
            const int gy = (int)lroundf(baseline) + y0;
            for (int y = 0; y < gh; ++y) {
                const int py = gy + y;
                if (py < 0 || py >= canvas_h) continue;
                for (int x = 0; x < gw; ++x) {
                    const int px = gx + x;
                    if (px < 0 || px >= canvas_w) continue;
                    const float coverage = bitmap[(size_t)y * (size_t)gw + x] / 255.0f;
                    float* dst = &pixels[((size_t)py * canvas_w + px) * 4];
                    if (opaque_background) {
                        for (int channel = 0; channel < 3; ++channel)
                            dst[channel] += (brightness - dst[channel]) * coverage;
                    } else if (coverage > dst[3]) {
                        dst[0] = dst[1] = dst[2] = brightness;
                        dst[3] = coverage;
                    }
                }
            }
        }
        pen_x += advanceOf(font, cp, next);
    }
}

// Scales the glyph texture down when the box is huge, keeping the layout itself unchanged.
float pixelScaleFor(const TextObjectConfig& config) {
    float pixel_scale = kMaxTexturePixelScale;
    const float largest_side = std::max(config.size[0], config.size[1]);
    if (largest_side > 0.0f) pixel_scale = std::min(pixel_scale, (float)kMaxTextureSize / largest_side);
    return pixel_scale;
}

// Without a width limit Wallpaper Engine only breaks lines at explicit newlines.
float layoutWidthFor(const TextObjectConfig& config, float pixel_scale) {
    const bool width_limited = config.limit_width && config.maxwidth > 0.0f;
    return width_limited ? config.maxwidth * pixel_scale : 1.0e9f;
}

struct LineMetrics {
    float ascent_px = 0.0f;
    float line_advance = 0.0f;
};

LineMetrics lineMetricsFor(const stbtt_fontinfo& font, float scale) {
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    return {ascent * scale, (ascent - descent + line_gap) * scale};
}

// Top of the text block for the vertical alignment; centred unless pinned to an edge.
float blockStartY(const TextObjectConfig& config, int height, float pad, float block_height) {
    if (config.vertical_align == "top") return pad;
    if (config.vertical_align == "bottom") return (float)height - pad - block_height;
    return pad + ((float)height - 2.0f * pad - block_height) * 0.5f;
}

// Left edge of a line for the horizontal alignment; centred unless pinned to an edge.
float lineStartX(const TextObjectConfig& config, int width, float pad, float line_width) {
    if (config.horizontal_align == "left") return pad;
    if (config.horizontal_align == "right") return (float)width - pad - line_width;
    return pad + ((float)width - 2.0f * pad - line_width) * 0.5f;
}

std::array<float, 4> backgroundPixelFor(const TextRasterRequest& request) {
    std::array<float, 4> background = {};
    if (!request.config.opaque_background) return background;
    background[3] = 1.0f;
    for (int channel = 0; channel < 3; ++channel)
        background[channel] = request.config.background_color[(size_t)channel] * request.config.background_brightness /
                              std::max(request.tint[channel], 1.0f / 255.0f);
    return background;
}

void fillBackground(std::vector<float>& pixels, const std::array<float, 4>& background) {
    for (size_t i = 0; i < pixels.size(); i += 4) std::copy(background.begin(), background.end(), pixels.begin() + i);
}

}  // namespace

bool rasterizeText(const TextRasterRequest& request, TextRasterResult& out) {
    const TextObjectConfig& config = request.config;
    const std::shared_ptr<const LoadedFont> loaded = fontRepository().font(request.font_path);
    if (!loaded) return false;
    const stbtt_fontinfo& font = loaded->info;

    const float pixel_scale = pixelScaleFor(config);
    const float font_px = std::max(1.0f, config.pointsize * kDesignUnitsPerPoint * pixel_scale);
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, font_px);
    const TextFont text_font{&font, font_px, scale};
    const LineMetrics metrics = lineMetricsFor(font, scale);

    std::vector<std::string> lines = layoutLines(text_font, config.text, layoutWidthFor(config, pixel_scale));
    if (lines.empty()) return false;
    if (config.limit_rows && config.max_rows > 0 && lines.size() > (size_t)config.max_rows)
        lines.resize((size_t)config.max_rows);

    const float block_height = (float)lines.size() * metrics.line_advance;
    float natural_width = 0.0f;
    for (const auto& line : lines) natural_width = std::max(natural_width, measureText(text_font, line));

    // Padding is empty border around the text, so effects have room to draw outside the glyphs.
    const float pad = std::max(0.0f, config.padding) * pixel_scale;
    const int width =
        std::clamp((int)ceilf(std::max(config.size[0] * pixel_scale, natural_width + 2.0f * pad)), 1, kMaxTextureSize);
    const int height =
        std::clamp((int)ceilf(std::max(config.size[1] * pixel_scale, block_height + 2.0f * pad)), 1, kMaxTextureSize);
    const float start_y = blockStartY(config, height, pad, block_height);

    out.pixels.resize((size_t)width * height * 4);
    fillBackground(out.pixels, backgroundPixelFor(request));
    for (size_t i = 0; i < lines.size(); ++i) {
        const float line_width = measureText(text_font, lines[i]);
        const float pen_x = lineStartX(config, width, pad, line_width);
        const float baseline = start_y + (float)i * metrics.line_advance + metrics.ascent_px;
        blitLine(out.pixels, width, height, text_font, lines[i], pen_x, baseline, config.opaque_background,
                 config.brightness);
    }

    out.width = width;
    out.height = height;
    out.full_width = width;
    out.full_height = height;
    out.content_x = 0;
    out.content_y = 0;
    out.cropped = false;
    out.pixel_scale = pixel_scale;
    out.size[0] = (float)width / pixel_scale;
    out.size[1] = (float)height / pixel_scale;
    return true;
}

void cropToContent(TextRasterResult& raster) {
    const int full_w = raster.full_width;
    const int full_h = raster.full_height;
    int min_x = full_w;
    int min_y = full_h;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < full_h; ++y) {
        const float* row = &raster.pixels[(size_t)y * full_w * 4];
        for (int x = 0; x < full_w; ++x) {
            if (row[x * 4 + 3] <= 0.0f) continue;
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
    }
    // Nothing visible, or the whole raster is visible: keep the full texture.
    if (max_x < 0) return;
    const int crop_w = max_x - min_x + 1;
    const int crop_h = max_y - min_y + 1;
    if (crop_w == full_w && crop_h == full_h) return;

    std::vector<float> cropped((size_t)crop_w * crop_h * 4);
    for (int y = 0; y < crop_h; ++y) {
        const float* source = &raster.pixels[((size_t)(min_y + y) * full_w + min_x) * 4];
        std::copy(source, source + (size_t)crop_w * 4, cropped.begin() + (size_t)y * crop_w * 4);
    }
    raster.pixels = std::move(cropped);
    raster.width = crop_w;
    raster.height = crop_h;
    raster.content_x = min_x;
    raster.content_y = min_y;
    raster.cropped = true;
}
