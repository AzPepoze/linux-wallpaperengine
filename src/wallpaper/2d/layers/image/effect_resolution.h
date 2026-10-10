#pragma once

#include <string_view>

namespace effect_resolution {
// Smaller targets keep the authored g_TextureNResolution, so texel-offset shaders match.
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
}  // namespace effect_resolution
