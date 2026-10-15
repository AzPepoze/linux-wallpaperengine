#pragma once

#include <string>
#include <string_view>

// How large textures are sampled when they are drawn smaller than their size. Off samples the base level only.
// Auto uses the stored or generated mip levels for textures larger than the output. The shrink methods resize the base
// level once on load and again when the output size changes.
namespace downscale {
enum class Method { Off, Auto, Mipmap, Aniso, Box, Lanczos };

inline bool parse(std::string_view text, Method& out) {
    if (text == "off") out = Method::Off;
    else if (text == "auto") out = Method::Auto;
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
        case Method::Auto: return "auto (mips for textures larger than the output)";
        case Method::Mipmap: return "mipmap (trilinear)";
        case Method::Aniso: return "aniso (trilinear, 8x)";
        case Method::Box: return "box (area average)";
        case Method::Lanczos: return "lanczos (Lanczos3)";
    }
    return "off (bilinear)";
}

// True when the method can use mip levels for some textures, so the sampler needs mip filtering.
inline bool usesMipChain(Method method) {
    return method == Method::Auto || method == Method::Mipmap || method == Method::Aniso;
}

// True when a texture of this size should get mip levels, given the output it is drawn on.
inline bool wantsMips(Method method, int texture_width, int texture_height, int output_width, int output_height) {
    if (method == Method::Mipmap || method == Method::Aniso) return true;
    if (method != Method::Auto) return false;
    if (output_width <= 0 || output_height <= 0) return false;
    return texture_width > output_width || texture_height > output_height;
}

// Resize the base level to the displayed size.
inline bool resizesPixels(Method method) {
    return method == Method::Box || method == Method::Lanczos;
}
}  // namespace downscale
