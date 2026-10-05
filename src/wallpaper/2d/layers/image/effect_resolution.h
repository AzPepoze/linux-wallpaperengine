#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace effect_resolution {
// Effect targets may be smaller than the authored layer, with `g_TextureNResolution` still reporting the authored
// size, so texel-offset shaders (blur, shine) produce the same UV offsets. Only shaders that read the pixel position
// itself would change with the target size.
inline bool readsPixelPosition(std::string_view source) {
    for (std::string_view token : {"gl_FragCoord", "dFdx", "dFdy", "fwidth", "ddx", "ddy"}) {
        if (source.find(token) != std::string_view::npos) return true;
    }
    return false;
}

// Bindings that read the accumulated scene (full-screen buffers) must keep the physical size.
inline bool isCompositeBinding(std::string_view name) {
    if (name.rfind("_rt_", 0) != 0) return false;
    return name.find("FrameBuffer") != std::string_view::npos || name.rfind("_rt_imageLayerComposite_", 0) == 0;
}

inline std::pair<int, int> targetSize(int source_width, int source_height, double displayed_width,
                                      double displayed_height) {
    if (source_width <= 0 || source_height <= 0) return {1, 1};
    if (!std::isfinite(displayed_width) || !std::isfinite(displayed_height)) return {source_width, source_height};
    const double ratio =
        std::min(1.0, std::max(std::abs(displayed_width) / source_width, std::abs(displayed_height) / source_height));
    return {std::max(1, (int)std::ceil(source_width * ratio)), std::max(1, (int)std::ceil(source_height * ratio))};
}
}  // namespace effect_resolution
