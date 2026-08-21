#ifndef IMAGE_PARSER_H
#define IMAGE_PARSER_H

#include <string>

#include "formats/wallpaper_engine/scene/scene_document.h"

struct ImageObjectConfig {
    std::string name;
    std::string asset_path;
    float width = 0.0f;
    float height = 0.0f;
    bool is_model = false;
};

class ImageParser {
   public:
    static ImageObjectConfig parse(const wallpaper_engine::SceneObjectDocument& document);
};

#endif  // IMAGE_PARSER_H
