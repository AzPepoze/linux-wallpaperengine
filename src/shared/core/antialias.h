#pragma once

#include <string>
#include <string_view>

// Edge smoothing for the composed scene. Off keeps the current single-sample path.
namespace antialias {
enum class Mode { Off, Fxaa, Smaa, Msaa2, Msaa4, Msaa8 };

inline bool parse(std::string_view text, Mode& out) {
    if (text == "off") out = Mode::Off;
    else if (text == "fxaa") out = Mode::Fxaa;
    else if (text == "smaa") out = Mode::Smaa;
    else if (text == "msaa2") out = Mode::Msaa2;
    else if (text == "msaa4") out = Mode::Msaa4;
    else if (text == "msaa8") out = Mode::Msaa8;
    else return false;
    return true;
}

inline std::string describe(Mode mode) {
    switch (mode) {
        case Mode::Off: return "off";
        case Mode::Fxaa: return "FXAA";
        case Mode::Smaa: return "SMAA";
        case Mode::Msaa2: return "MSAA 2x";
        case Mode::Msaa4: return "MSAA 4x";
        case Mode::Msaa8: return "MSAA 8x";
    }
    return "off";
}

// Samples per pixel for the MSAA modes; 1 for everything else.
inline int sampleCount(Mode mode) {
    switch (mode) {
        case Mode::Msaa2: return 2;
        case Mode::Msaa4: return 4;
        case Mode::Msaa8: return 8;
        default: return 1;
    }
}

inline bool isMsaa(Mode mode) {
    return sampleCount(mode) > 1;
}

// FXAA and SMAA run as a post pass over the composed scene.
inline bool isPostPass(Mode mode) {
    return mode == Mode::Fxaa || mode == Mode::Smaa;
}
}  // namespace antialias
