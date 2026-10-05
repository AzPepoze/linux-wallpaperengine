#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "sokol_gfx.h"

inline size_t readbackPixelSize(sg_pixel_format format) {
    switch (format) {
        case SG_PIXELFORMAT_R8:
            return 1;
        case SG_PIXELFORMAT_RG8:
            return 2;
        case SG_PIXELFORMAT_RGBA8:
        case SG_PIXELFORMAT_BGRA8:
            return 4;
        case SG_PIXELFORMAT_RGBA16F:
            return 8;
        case SG_PIXELFORMAT_RGBA32F:
            return 16;
        default:
            return 0;
    }
}

inline float readbackHalf(uint16_t bits) {
    const int exponent = (bits >> 10) & 31;
    const int fraction = bits & 1023;
    const float sign = bits & 0x8000 ? -1.0f : 1.0f;
    if (exponent == 31) return fraction ? 0.0f : sign * INFINITY;
    return sign * (exponent ? std::ldexp((float)(1024 + fraction), exponent - 25) : std::ldexp((float)fraction, -24));
}

inline void convertReadbackPixels(const uint8_t* source, uint8_t* output, size_t count, sg_pixel_format format) {
    const size_t stride = readbackPixelSize(format);
    for (size_t pixel = 0; pixel < count; ++pixel) {
        for (int channel = 0; channel < 4; ++channel) {
            float value = channel == 3 ? 1.0f : 0.0f;
            if (format == SG_PIXELFORMAT_RGBA16F) {
                uint16_t half;
                memcpy(&half, source + pixel * stride + channel * 2, 2);
                value = readbackHalf(half);
            } else if (format == SG_PIXELFORMAT_RGBA32F) {
                memcpy(&value, source + pixel * stride + channel * 4, 4);
            } else if ((size_t)channel < stride) {
                const int index = format == SG_PIXELFORMAT_BGRA8 && channel < 3 ? 2 - channel : channel;
                value = source[pixel * stride + index] / 255.0f;
            }
            if (std::isnan(value)) value = 0;
            output[pixel * 4 + channel] = (uint8_t)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        }
    }
}
