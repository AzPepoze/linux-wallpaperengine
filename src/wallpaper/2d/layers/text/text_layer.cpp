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

float measureText(const stbtt_fontinfo& font, float scale, const std::string& text) {
    float width = 0.0f;
    size_t index = 0;
    while (index < text.size()) {
        const int cp = nextCodepoint(text, index);
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&font, cp, &advance, &bearing);
        width += advance * scale;
    }
    return width;
}

std::vector<std::string> wrapWords(const stbtt_fontinfo& font, float scale, const std::string& paragraph,
                                   float max_width) {
    std::vector<std::string> lines;
    std::string line;
    size_t index = 0;
    while (index <= paragraph.size()) {
        const size_t space = paragraph.find(' ', index);
        const std::string word =
            paragraph.substr(index, space == std::string::npos ? std::string::npos : space - index);
        if (!word.empty()) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (line.empty() || measureText(font, scale, candidate) <= max_width + 0.5f) {
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

std::vector<std::string> layoutLines(const stbtt_fontinfo& font, float scale, const std::string& text,
                                     float max_width) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        std::string paragraph = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!paragraph.empty() && paragraph.back() == '\r') paragraph.pop_back();
        for (auto& line : wrapWords(font, scale, paragraph, max_width)) lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

void blitLine(std::vector<uint32_t>& pixels, int canvas_w, int canvas_h, const stbtt_fontinfo& font, float scale,
              const std::string& line, float pen_x, float baseline) {
    size_t index = 0;
    while (index < line.size()) {
        const int cp = nextCodepoint(line, index);
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&font, cp, &advance, &bearing);

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, cp, scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0;
        const int gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<uint8_t> bitmap((size_t)gw * (size_t)gh);
            stbtt_MakeCodepointBitmap(&font, bitmap.data(), gw, gh, gw, scale, scale, cp);
            const int gx = (int)lroundf(pen_x) + x0;
            const int gy = (int)lroundf(baseline) + y0;
            for (int y = 0; y < gh; ++y) {
                const int py = gy + y;
                if (py < 0 || py >= canvas_h) continue;
                for (int x = 0; x < gw; ++x) {
                    const int px = gx + x;
                    if (px < 0 || px >= canvas_w) continue;
                    const uint32_t coverage = bitmap[(size_t)y * (size_t)gw + x];
                    uint32_t& dst = pixels[(size_t)py * (size_t)canvas_w + px];
                    if (coverage > (dst >> 24)) dst = (coverage << 24) | 0x00FFFFFFu;
                }
            }
        }
        pen_x += advance * scale;
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
    if (!doc.text.script.empty()) {
        layer->script_ = std::make_unique<SceneScript>();
        layer->script_->setLayerId(doc.node.id);
        layer->script_->setProperty("text");
        if (layer->script_->load(doc.text.script, doc.text.script_properties_json)) {
            layer->script_timer_ = 1.0f;  // evaluate on the first update
            LOG_I("Text layer '%s': SceneScript loaded", config.name.c_str());
        } else {
            layer->script_.reset();
        }
    }
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
    } else {
        return false;
    }
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
    else
        return false;
    return true;
}

bool TextLayer::propertySetNumber(const std::string& name, double value) {
    if (name == "alpha") {
        config_.alpha = (float)value;
        return true;
    }
    if (name == "pointsize")
        config_.pointsize = (float)value;
    else if (name == "maxwidth")
        config_.maxwidth = (float)value;
    else if (name == "maxrows")
        config_.max_rows = (int)value;
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

void TextLayer::update(float dt, EngineContext& ctx) {
    if (script_ && script_->valid()) {
        script_timer_ += dt;
        if (script_timer_ >= 0.25f) {
            script_timer_ = 0.0f;
            ScriptValue text = ScriptValue::makeString(current_text_);
            if (script_->updateValue(text) && !text.text.empty()) config_.text = text.text;
        }
    }
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
    if (ctx.asset_mgr.resolvePath(candidate.c_str(), resolved, sizeof(resolved))) {
        font_path_ = resolved;
        return true;
    }
    LOG_W("Text layer '%s': font not found: %s", config_.name.c_str(), config_.font.c_str());
    return false;
}

bool TextLayer::rasterize(std::vector<uint32_t>& pixels, int& width, int& height, float& pixel_scale) const {
    const LoadedFont* loaded = loadFont(font_path_);
    if (!loaded) return false;
    const stbtt_fontinfo& font = loaded->info;

    // Keep large boxes under the texture size cap without changing their layout.
    pixel_scale = kMaxTexturePixelScale;
    const float largest_side = std::max(config_.size[0], config_.size[1]);
    if (largest_side > 0.0f) pixel_scale = std::min(pixel_scale, (float)kMaxTextureSize / largest_side);

    const float font_px = std::max(1.0f, config_.pointsize * kDesignUnitsPerPoint * pixel_scale);
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, font_px);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    const float ascent_px = ascent * scale;
    const float line_advance = (ascent - descent + line_gap) * scale;

    // Without a width limit Wallpaper Engine only breaks lines at explicit newlines.
    const float layout_width = config_.maxwidth > 0.0f ? config_.maxwidth * pixel_scale : 1.0e9f;

    std::vector<std::string> lines = layoutLines(font, scale, config_.text, layout_width);
    if (lines.empty()) return false;
    if (config_.max_rows > 0 && lines.size() > (size_t)config_.max_rows) lines.resize((size_t)config_.max_rows);

    const float block_height = (float)lines.size() * line_advance;
    float natural_width = 0.0f;
    for (const auto& line : lines) natural_width = std::max(natural_width, measureText(font, scale, line));

    width = std::clamp(config_.size[0] > 0.0f ? (int)lroundf(config_.size[0] * pixel_scale) : (int)ceilf(natural_width),
                       1, kMaxTextureSize);
    height = std::clamp(config_.size[1] > 0.0f ? (int)lroundf(config_.size[1] * pixel_scale) : (int)ceilf(block_height),
                        1, kMaxTextureSize);

    float start_y = (height - block_height) * 0.5f;
    if (config_.vertical_align == "top")
        start_y = 0.0f;
    else if (config_.vertical_align == "bottom")
        start_y = (float)height - block_height;

    pixels.assign((size_t)width * (size_t)height, 0u);
    for (size_t i = 0; i < lines.size(); ++i) {
        const float line_width = measureText(font, scale, lines[i]);
        float pen_x = (width - line_width) * 0.5f;
        if (config_.horizontal_align == "left")
            pen_x = 0.0f;
        else if (config_.horizontal_align == "right")
            pen_x = (float)width - line_width;

        const float baseline = start_y + (float)i * line_advance + ascent_px;
        blitLine(pixels, width, height, font, scale, lines[i], pen_x, baseline);
    }
    return true;
}

bool TextLayer::rebuild(EngineContext& ctx) {
    current_text_ = config_.text;
    if (config_.text.empty()) return false;
    if (!resolveFontPath(ctx)) return false;

    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    float pixel_scale = 1.0f;
    if (!rasterize(pixels, width, height, pixel_scale) || pixels.empty()) return false;

    if (config_.size[0] <= 0.0f) size[0] = (float)width / pixel_scale;
    if (config_.size[1] <= 0.0f) size[1] = (float)height / pixel_scale;

    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = {pixels.data(), pixels.size() * sizeof(uint32_t)};
    const sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) return false;

    cached_view = {};
    img = image;
    sg_view_desc view_desc = {};
    view_desc.texture.image = img;
    cached_view = sg_make_view(&view_desc);
    return true;
}
