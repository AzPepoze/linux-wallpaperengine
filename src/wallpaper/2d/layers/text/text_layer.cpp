#include "text_layer.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <math.h>
#include <stb/stb_truetype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <map>

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "wallpaper/2d/alpha_curve.h"

namespace {

// Wallpaper Engine point sizes are authored in design units at a fixed ratio.
constexpr float kDesignUnitsPerPoint = 4.0f;
// Supersampling factor of the generated glyph texture for crisp text.
constexpr float kMaxTexturePixelScale = 2.0f;
constexpr int kMaxTextureSize = 4096;

struct LoadedFont {
    std::vector<uint8_t> data;
    stbtt_fontinfo info = {};
    bool ready = false;
};

std::map<std::string, LoadedFont>& fontCache() {
    static std::map<std::string, LoadedFont> cache;
    return cache;
}

const LoadedFont* loadFont(const std::string& path) {
    LoadedFont& font = fontCache()[path];
    if (font.ready) return &font;

    if (!vfs::readAll(path.c_str(), font.data) || font.data.empty()) return nullptr;

    const int offset = stbtt_GetFontOffsetForIndex(font.data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&font.info, font.data.data(), offset)) return nullptr;
    font.ready = true;
    return &font;
}

// Wallpaper Engine falls back to installed system fonts for characters the authored font lacks (CJK track names in a
// Latin display font, for example). The fallback is found per 256-codepoint block through fontconfig and cached.
const LoadedFont* fallbackFont(int codepoint) {
    static std::map<int, std::string> block_paths;
    static std::map<std::string, LoadedFont> system_fonts;
    const int block = codepoint >> 8;
    auto found = block_paths.find(block);
    if (found == block_paths.end()) {
        std::string path;
        char command[96];
        snprintf(command, sizeof(command), "fc-match -f '%%{file}' ':charset=%x' 2>/dev/null", (unsigned)codepoint);
        if (FILE* pipe = popen(command, "r")) {
            char buffer[1024] = {};
            if (fgets(buffer, sizeof(buffer), pipe)) path = buffer;
            pclose(pipe);
        }
        found = block_paths.emplace(block, path).first;
    }
    if (found->second.empty()) return nullptr;

    LoadedFont& font = system_fonts[found->second];
    if (font.ready) return &font;
    if (FILE* file = fopen(found->second.c_str(), "rb")) {
        uint8_t chunk[65536];
        size_t count = 0;
        while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) font.data.insert(font.data.end(), chunk, chunk + count);
        fclose(file);
    }
    if (font.data.empty()) return nullptr;
    const int offset = stbtt_GetFontOffsetForIndex(font.data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&font.info, font.data.data(), offset)) return nullptr;
    font.ready = true;
    return &font;
}

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
        if (const LoadedFont* fallback = fallbackFont(codepoint))
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

}  // namespace

TextLayer::TextLayer(const char* name) : ImageLayer(name, (sg_image){SG_INVALID_ID}) {}

TextLayer* TextLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    TextObjectConfig config = TextParser::parse(doc);
    TextLayer* layer = new TextLayer(config.name.c_str());
    layer->initFromDocument(doc, ctx);
    layer->alpha_document = doc.image;
    layer->config_ = config;
    layer->size[0] = doc.text.size[0];
    layer->size[1] = doc.text.size[1];
    layer->tint[0] = config.color[0];
    layer->tint[1] = config.color[1];
    layer->tint[2] = config.color[2];
    layer->tint[3] = std::clamp(config.alpha, 0.0f, 1.0f);
    layer->rebuild(ctx);
    LOG_I("Created text layer '%s' (font='%s', pointsize=%.1f)", config.name.c_str(), config.font.c_str(),
          config.pointsize);
    return layer;
}

bool TextLayer::propertyGetString(const std::string& name, std::string& out) const {
    if (name == "text")
        out = config_.text;
    else if (name == "font")
        out = config_.font;
    else if (name == "horizontalalign")
        out = config_.horizontal_align;
    else if (name == "verticalalign")
        out = config_.vertical_align;
    else if (name == "anchor")
        out = config_.anchor;
    else
        return false;
    return true;
}

bool TextLayer::propertySetString(const std::string& name, const std::string& value) {
    if (name == "text") {
        config_.text = value;
        return true;
    }
    if (name == "font") {
        config_.font = value;
        font_path_.clear();
    } else if (name == "horizontalalign") {
        config_.horizontal_align = value;
    } else if (name == "verticalalign") {
        config_.vertical_align = value;
    } else if (name == "anchor") {
        config_.anchor = value;
    } else {
        return false;
    }
    needs_rebuild_ = true;
    return true;
}

bool TextLayer::propertyGetBool(const std::string& name, bool& out) const {
    if (name == "opaquebackground")
        out = config_.opaque_background;
    else if (name == "limitrows")
        out = config_.limit_rows;
    else if (name == "limitwidth")
        out = config_.limit_width;
    else
        return false;
    return true;
}

bool TextLayer::propertySetBool(const std::string& name, bool value) {
    if (name == "opaquebackground")
        config_.opaque_background = value;
    else if (name == "limitrows")
        config_.limit_rows = value;
    else if (name == "limitwidth")
        config_.limit_width = value;
    else
        return false;
    needs_rebuild_ = true;
    return true;
}

bool TextLayer::propertyGetVector(const std::string& name, double out[3]) const {
    if (name != "backgroundcolor") return false;
    for (size_t i = 0; i < 3; ++i) out[i] = config_.background_color[i];
    return true;
}

bool TextLayer::propertySetVector(const std::string& name, const double value[3]) {
    if (name != "backgroundcolor") return false;
    for (size_t i = 0; i < 3; ++i) config_.background_color[i] = (float)value[i];
    needs_rebuild_ = true;
    return true;
}

bool TextLayer::propertyGetNumber(const std::string& name, double& out) const {
    if (name == "pointsize")
        out = config_.pointsize;
    else if (name == "maxwidth")
        out = config_.maxwidth;
    else if (name == "maxrows")
        out = config_.max_rows;
    else if (name == "alpha")
        out = config_.alpha;
    else if (name == "brightness")
        out = config_.brightness;
    else if (name == "backgroundbrightness")
        out = config_.background_brightness;
    else if (name == "padding")
        out = config_.padding;
    else
        return false;
    return true;
}

bool TextLayer::propertySetNumber(const std::string& name, double value) {
    if (name == "alpha") {
        config_.alpha = (float)value;
        return true;
    }
    if (name == "brightness")
        config_.brightness = (float)value;
    else if (name == "backgroundbrightness")
        config_.background_brightness = (float)value;
    else if (name == "pointsize")
        config_.pointsize = (float)value;
    else if (name == "maxwidth")
        config_.maxwidth = (float)value;
    else if (name == "maxrows")
        config_.max_rows = (int)value;
    else if (name == "padding")
        config_.padding = (float)value;
    else
        return false;
    needs_rebuild_ = true;
    return true;
}

ImageLayer::ScreenRect TextLayer::screenRect(EngineContext& ctx) const {
    ScreenRect rect = ImageLayer::screenRect(ctx);
    // Wallpaper Engine anchors a text object at the corner selected by the
    // alignment (e.g. the origin is the top-left of the text for left/top) and
    // rotates around it. The base centres the sprite on the node, so offset it
    // to put the alignment corner on the node instead.
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    if (config_.horizontal_align == "left")
        anchor_x = -rect.width * 0.5f;
    else if (config_.horizontal_align == "right")
        anchor_x = rect.width * 0.5f;
    if (config_.vertical_align == "top")
        anchor_y = -rect.height * 0.5f;
    else if (config_.vertical_align == "bottom")
        anchor_y = rect.height * 0.5f;

    if (anchor_x != 0.0f || anchor_y != 0.0f) {
        const float angle = rect.rotation * (float)M_PI / 180.0f;
        rect.x -= cosf(angle) * anchor_x - sinf(angle) * anchor_y;
        rect.y -= sinf(angle) * anchor_x + cosf(angle) * anchor_y;
    }
    return rect;
}

void TextLayer::update(float, EngineContext& ctx) {
    if (config_.text != current_text_ || needs_rebuild_) {
        needs_rebuild_ = false;
        rebuild(ctx);
    }
    tint[3] = std::clamp(config_.alpha, 0.0f, 1.0f) * evaluateImageAlpha(alpha_document, ctx.time);
    if (!is_fullscreen) renderEffectChain(ctx);
}

bool TextLayer::resolveFontPath(EngineContext& ctx) {
    if (!font_path_.empty()) return true;

    std::string candidate = config_.font;
    if (candidate.empty() || candidate.rfind("systemfont", 0) == 0) candidate = "fonts/NotoSans-Regular.ttf";

    char resolved[1024] = {};
    if (ctx.asset_mgr->resolvePath(candidate.c_str(), resolved, sizeof(resolved))) {
        font_path_ = resolved;
        return true;
    }
    LOG_W("Text layer '%s': font not found: %s", config_.name.c_str(), config_.font.c_str());
    return false;
}

bool TextLayer::rasterize(std::vector<float>& pixels, int& width, int& height, float& pixel_scale) const {
    const LoadedFont* loaded = loadFont(font_path_);
    if (!loaded) return false;
    const stbtt_fontinfo& font = loaded->info;

    // Keep large boxes under the texture size cap without changing their layout.
    pixel_scale = kMaxTexturePixelScale;
    const float largest_side = std::max(config_.size[0], config_.size[1]);
    if (largest_side > 0.0f) pixel_scale = std::min(pixel_scale, (float)kMaxTextureSize / largest_side);

    const float font_px = std::max(1.0f, config_.pointsize * kDesignUnitsPerPoint * pixel_scale);
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, font_px);
    const TextFont text_font{&font, font_px, scale};
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    const float ascent_px = ascent * scale;
    const float line_advance = (ascent - descent + line_gap) * scale;

    // Without a width limit Wallpaper Engine only breaks lines at explicit newlines.
    const bool width_limited = config_.limit_width && config_.maxwidth > 0.0f;
    const float layout_width = width_limited ? config_.maxwidth * pixel_scale : 1.0e9f;

    std::vector<std::string> lines = layoutLines(text_font, config_.text, layout_width);
    if (lines.empty()) return false;
    if (config_.limit_rows && config_.max_rows > 0 && lines.size() > (size_t)config_.max_rows)
        lines.resize((size_t)config_.max_rows);

    const float block_height = (float)lines.size() * line_advance;
    float natural_width = 0.0f;
    for (const auto& line : lines) natural_width = std::max(natural_width, measureText(text_font, line));

    // Padding is empty border around the text, so effects have room to draw outside the glyphs.
    const float pad = std::max(0.0f, config_.padding) * pixel_scale;
    width =
        std::clamp((int)ceilf(std::max(config_.size[0] * pixel_scale, natural_width + 2.0f * pad)), 1, kMaxTextureSize);
    height =
        std::clamp((int)ceilf(std::max(config_.size[1] * pixel_scale, block_height + 2.0f * pad)), 1, kMaxTextureSize);

    float start_y = pad + ((float)height - 2.0f * pad - block_height) * 0.5f;
    if (config_.vertical_align == "top")
        start_y = pad;
    else if (config_.vertical_align == "bottom")
        start_y = (float)height - pad - block_height;

    // Glyph brightness is stored before effects, without clipping HDR values or changing coverage.
    float background[4] = {};
    if (config_.opaque_background) {
        background[3] = 1.0f;
        for (int channel = 0; channel < 3; ++channel)
            background[channel] = config_.background_color[(size_t)channel] * config_.background_brightness /
                                  std::max(tint[channel], 1.0f / 255.0f);
    }
    pixels.resize((size_t)width * height * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) std::copy(background, background + 4, pixels.begin() + i);
    for (size_t i = 0; i < lines.size(); ++i) {
        const float line_width = measureText(text_font, lines[i]);
        float pen_x = pad + ((float)width - 2.0f * pad - line_width) * 0.5f;
        if (config_.horizontal_align == "left")
            pen_x = pad;
        else if (config_.horizontal_align == "right")
            pen_x = (float)width - pad - line_width;

        const float baseline = start_y + (float)i * line_advance + ascent_px;
        blitLine(pixels, width, height, text_font, lines[i], pen_x, baseline, config_.opaque_background,
                 config_.brightness);
    }
    return true;
}

bool TextLayer::rebuild(EngineContext& ctx) {
    current_text_ = config_.text;
    if (!resolveFontPath(ctx)) return false;

    std::vector<float> pixels;
    int width = 0;
    int height = 0;
    float pixel_scale = 1.0f;
    if (!rasterize(pixels, width, height, pixel_scale) || pixels.empty()) return false;

    size[0] = (float)width / pixel_scale;
    size[1] = (float)height / pixel_scale;

    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
    desc.data.mip_levels[0] = {pixels.data(), pixels.size() * sizeof(float)};
    const sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) return false;

    cached_view = {};
    img = image;
    sg_view_desc view_desc = {};
    view_desc.texture.image = img;
    cached_view = sg_make_view(&view_desc);
    return true;
}
