#include "text_parser.h"

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
    config.horizontal_align = document.text.horizontal_align;
    config.vertical_align = document.text.vertical_align;
    return config;
}
