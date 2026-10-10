#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

// Render-target sizing for layers: effect chains, puppets and models.
namespace resolution {
struct Setting {
    enum class Mode { Auto, Native, Fixed };
    Mode mode = Mode::Auto;
    int width = 0;   // Fixed mode only: reference output width
    int height = 0;  // Fixed mode only: reference output height
};

inline bool parseDimension(std::string_view text, int& out) {
    if (text.empty() || text.size() > 6) return false;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value <= 0) return false;
    out = value;
    return true;
}

// Accepts "auto", "native" or "WxH". Returns false and leaves `out` unchanged otherwise.
inline bool parse(std::string_view text, Setting& out) {
    if (text == "auto") {
        out = Setting{};
        return true;
    }
    if (text == "native") {
        out = Setting{Setting::Mode::Native, 0, 0};
        return true;
    }
    const size_t separator = text.find('x');
    if (separator == std::string_view::npos) return false;
    int width = 0;
    int height = 0;
    if (!parseDimension(text.substr(0, separator), width)) return false;
    if (!parseDimension(text.substr(separator + 1), height)) return false;
    out = Setting{Setting::Mode::Fixed, width, height};
    return true;
}

// Text for the inspector and logs.
inline std::string describe(const Setting& setting) {
    if (setting.mode == Setting::Mode::Native) return "native (authored size)";
    if (setting.mode == Setting::Mode::Fixed)
        return std::to_string(setting.width) + "x" + std::to_string(setting.height) + " reference";
    return "auto (follows the output)";
}

// Scale that maps the physical output onto the reference size; 1 unless a fixed size is set.
inline double referenceRatio(const Setting& setting, double physical_width, double physical_height) {
    if (setting.mode != Setting::Mode::Fixed || physical_width <= 0.0 || physical_height <= 0.0) return 1.0;
    return std::min(setting.width / physical_width, setting.height / physical_height);
}

inline std::pair<int, int> targetSize(int source_width, int source_height, double displayed_width,
                                      double displayed_height) {
    if (source_width <= 0 || source_height <= 0) return {1, 1};
    if (!std::isfinite(displayed_width) || !std::isfinite(displayed_height)) return {source_width, source_height};
    const double ratio =
        std::min(1.0, std::max(std::abs(displayed_width) / source_width, std::abs(displayed_height) / source_height));
    return {std::max(1, (int)std::ceil(source_width * ratio)), std::max(1, (int)std::ceil(source_height * ratio))};
}
}  // namespace resolution
