#ifndef TEXT_PARSER_H
#define TEXT_PARSER_H

#include <array>
#include <string>

#include "wallpaper/2d/parser/scene_document.h"

struct TextObjectConfig {
    std::string name;
    std::string text;
    std::string font;
    float pointsize = 12.0f;
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    std::array<float, 2> size = {0.0f, 0.0f};
    float maxwidth = 0.0f;
    int max_rows = 0;
    std::string horizontal_align = "center";
    std::string vertical_align = "center";
};

class TextParser {
   public:
    static TextObjectConfig parse(const wallpaper_engine::SceneObjectDocument& document);
};

#endif  // TEXT_PARSER_H
