#pragma once

#include <string>
#include <string_view>

// How large textures are shrunk to the size they are drawn at. Every method except Off
// does its work once on load and again only when the displayed size changes.
namespace downscale {
enum class Method { Off, Mipmap, Aniso, Box, Lanczos };

inline bool parse(std::string_view text, Method& out) {
    if (text == "off") out = Method::Off;
    else if (text == "mipmap") out = Method::Mipmap;
    else if (text == "aniso") out = Method::Aniso;
    else if (text == "box") out = Method::Box;
    else if (text == "lanczos") out = Method::Lanczos;
    else return false;
    return true;
}

inline std::string describe(Method method) {
    switch (method) {
        case Method::Off: return "off (bilinear)";
        case Method::Mipmap: return "mipmap (trilinear)";
        case Method::Aniso: return "aniso (trilinear, 8x)";
        case Method::Box: return "box (area average)";
        case Method::Lanczos: return "lanczos (Lanczos3)";
    }
    return "off (bilinear)";
}

// Mip chain plus sampling, without resizing the base level.
inline bool usesMipChain(Method method) {
    return method == Method::Mipmap || method == Method::Aniso;
}

// Resize the base level to the displayed size.
inline bool resizesPixels(Method method) {
    return method == Method::Box || method == Method::Lanczos;
}
}  // namespace downscale
