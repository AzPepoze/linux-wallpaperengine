#ifndef TEXT_RASTER_H
#define TEXT_RASTER_H

#include <string>
#include <vector>

#include "text_parser.h"

struct TextRasterRequest {
    TextObjectConfig config;
    std::string font_path;  // resolved path, may be empty for fallback-only
    float tint[3] = {1.0f, 1.0f, 1.0f};
};

struct TextRasterResult {
    std::vector<float> pixels;  // RGBA32F, cropped when cropped is true
    int width = 0;              // of pixels
    int height = 0;
    int full_width = 0;  // of the uncropped raster
    int full_height = 0;
    int content_x = 0;  // where the pixels start inside the uncropped raster
    int content_y = 0;
    bool cropped = false;
    float pixel_scale = 1.0f;
    float size[2] = {0.0f, 0.0f};  // full raster width/height / pixel_scale
};

// Rasterises one text object on the CPU. Safe to call from any thread.
bool rasterizeText(const TextRasterRequest& request, TextRasterResult& out);

// Shrinks the raster to the box around its visible texels. Layout size is unchanged.
void cropToContent(TextRasterResult& raster);

#endif  // TEXT_RASTER_H
