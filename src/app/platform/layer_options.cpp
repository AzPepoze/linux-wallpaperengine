#include "app/platform/layer_options.h"

#include <ctype.h>
#include <stdlib.h>

#include <algorithm>

namespace layer_options {

namespace {
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return tolower(c); });
    return text;
}

bool parsePositive(const std::string& text, int& out) {
    if (text.empty() || text.size() > 6 || !std::all_of(text.begin(), text.end(), ::isdigit)) return false;
    out = atoi(text.c_str());
    return out > 0;
}

uint32_t edgeBit(const std::string& edge) {
    if (edge == "top") return kAnchorTop;
    if (edge == "bottom") return kAnchorBottom;
    if (edge == "left") return kAnchorLeft;
    if (edge == "right") return kAnchorRight;
    return 0;
}
}  // namespace

bool parseLayer(const std::string& name, Layer& out) {
    const std::string value = lower(name);
    if (value.empty() || value == "background") {
        out = Layer::Background;
    } else if (value == "bottom") {
        out = Layer::Bottom;
    } else if (value == "top") {
        out = Layer::Top;
    } else if (value == "overlay") {
        out = Layer::Overlay;
    } else {
        return false;
    }
    return true;
}

bool parseSize(const std::string& text, int& width, int& height) {
    const size_t separator = lower(text).find('x');
    if (separator == std::string::npos) return false;
    int w = 0;
    int h = 0;
    if (!parsePositive(text.substr(0, separator), w) || !parsePositive(text.substr(separator + 1), h)) return false;
    width = w;
    height = h;
    return true;
}

bool parseAnchor(const std::string& text, uint32_t& mask) {
    const std::string value = lower(text);
    if (value == "all") {
        mask = kAnchorAll;
        return true;
    }
    uint32_t result = 0;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find_first_of("-,", start);
        const std::string edge = value.substr(start, end == std::string::npos ? end : end - start);
        const uint32_t bit = edgeBit(edge);
        if (bit == 0) return false;
        result |= bit;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    mask = result;
    return true;
}

int findOutput(const std::vector<std::string>& names, const std::string& wanted) {
    if (wanted.empty()) return -1;
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == wanted) return static_cast<int>(i);
    const std::string folded = lower(wanted);
    for (size_t i = 0; i < names.size(); ++i)
        if (lower(names[i]) == folded) return static_cast<int>(i);
    return -1;
}

}  // namespace layer_options
