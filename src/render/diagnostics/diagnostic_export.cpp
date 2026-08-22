#include <cstdlib>
#include <fstream>

#include "render_diagnostics.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

void RenderDiagnostics::writeJsonToFile(const std::string& path, cJSON* json) {
    if (!json) return;
    char* rendered = cJSON_Print(json);
    if (!rendered) return;
    writeStringToFile(path, rendered);
    free(rendered);
}

void RenderDiagnostics::writeStringToFile(const std::string& path, const std::string& content) {
    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (output.is_open()) output << content;
}

bool RenderDiagnostics::writePng(const std::string& path, int width, int height, const uint8_t* rgba) {
    if (!rgba || width <= 0 || height <= 0) return false;
    // Set fast PNG compression level (1 = fastest, default = 8)
    stbi_write_png_compression_level = 1;
    return stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4) != 0;
}
