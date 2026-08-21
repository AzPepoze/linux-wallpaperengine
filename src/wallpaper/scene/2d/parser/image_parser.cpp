#include "image_parser.h"

ImageObjectConfig ImageParser::parse(const wallpaper_engine::SceneObjectDocument& document) {
    ImageObjectConfig config;
    config.name = document.name.empty() ? "Layer" : document.name;
    config.width = document.image.size[0];
    config.height = document.image.size[1];
    config.color = document.image.color;
    config.alpha = document.image.alpha;
    config.is_model = document.image.image.empty();
    config.asset_path = config.is_model ? document.image.model : document.image.image;
    return config;
}
