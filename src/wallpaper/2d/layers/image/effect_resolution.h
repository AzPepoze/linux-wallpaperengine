#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace effect_resolution {
// Fingerprints of the verified, expanded shake stages (including common.h).
// Ignore CR so Windows and Unix line endings have identical eligibility.
inline uint64_t fingerprint(std::string_view source) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char byte : source) {
        if (byte != '\r') hash = (hash ^ byte) * UINT64_C(1099511628211);
    }
    return hash;
}

inline bool verifiedShake(std::string_view vertex, std::string_view fragment) {
    return fingerprint(vertex) == UINT64_C(0xafdc66a32c97bde8) && fingerprint(fragment) == UINT64_C(0x86017c9437f8c7da);
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
