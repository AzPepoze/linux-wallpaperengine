#pragma once

#include <stdint.h>

#include <mutex>
#include <unordered_map>

#include "sokol_gfx.h"

// The size a texture was authored at. A GPU copy shrunk for memory reports this size, so layout and shaders keep the authored size.
namespace texture_size {
struct Size {
    int width = 0;
    int height = 0;
};

inline std::unordered_map<uint32_t, Size>& shrunkSources() {
    static std::unordered_map<uint32_t, Size> sources;
    return sources;
}

inline std::mutex& shrunkSourcesMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void recordShrunk(sg_image image, Size authored) {
    std::lock_guard<std::mutex> lock(shrunkSourcesMutex());
    shrunkSources()[image.id] = authored;
}

inline Size of(sg_image image) {
    {
        std::lock_guard<std::mutex> lock(shrunkSourcesMutex());
        const auto found = shrunkSources().find(image.id);
        if (found != shrunkSources().end()) return found->second;
    }
    const sg_image_desc desc = sg_query_image_desc(image);
    return {desc.width, desc.height};
}
}  // namespace texture_size
