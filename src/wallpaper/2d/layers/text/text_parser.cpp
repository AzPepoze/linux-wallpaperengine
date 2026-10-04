#include "text_parser.h"

#include <algorithm>

TextObjectConfig TextParser::parse(const wallpaper_engine::SceneObjectDocument& document) {
    TextObjectConfig config;
    config.name = document.name.empty() ? "Text" : document.name;
    config.text = document.text.text;
    config.font = document.text.font;
    config.pointsize = document.text.pointsize;
    config.color = document.text.color;
    config.alpha = document.text.alpha;
    config.size = document.text.size;
    config.maxwidth = document.text.maxwidth;
    config.limit_width = document.text.limit_width;
    config.max_rows = std::max(1, document.text.max_rows);
    config.limit_rows = document.text.limit_rows;
    config.horizontal_align = document.text.horizontal_align;
    config.vertical_align = document.text.vertical_align;
    config.opaque_background = document.text.opaque_background;
    config.background_color = document.text.background_color;
    config.padding = document.text.padding;
    config.anchor = document.text.anchor;
    return config;
}
