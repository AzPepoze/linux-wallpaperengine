#ifndef LAYER_OPTIONS_H
#define LAYER_OPTIONS_H

#include <stdint.h>

#include <string>
#include <vector>

// Pure helpers for the wlr-layer-shell command line options. Numeric values match the protocol enums.
namespace layer_options {

enum class Layer : uint32_t { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };

constexpr uint32_t kAnchorTop = 1;
constexpr uint32_t kAnchorBottom = 2;
constexpr uint32_t kAnchorLeft = 4;
constexpr uint32_t kAnchorRight = 8;
constexpr uint32_t kAnchorAll = kAnchorTop | kAnchorBottom | kAnchorLeft | kAnchorRight;

// An empty name selects the background layer.
bool parseLayer(const std::string& name, Layer& out);

// Parses "WxH" with both sides positive.
bool parseSize(const std::string& text, int& width, int& height);

// Parses "all" or edges joined by '-' or ',', e.g. "top-left".
bool parseAnchor(const std::string& text, uint32_t& mask);

// Index of the output called `wanted` (exact first, then case-insensitive), or -1.
int findOutput(const std::vector<std::string>& names, const std::string& wanted);

}  // namespace layer_options

#endif  // LAYER_OPTIONS_H
