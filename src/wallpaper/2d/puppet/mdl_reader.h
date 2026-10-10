#ifndef WALLPAPER_ENGINE_MDL_READER_H
#define WALLPAPER_ENGINE_MDL_READER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

namespace wallpaper_engine {
namespace mdl_detail {

// Bounds-checked little-endian cursor over an .mdl buffer; every read fails instead of running past the end.
struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    bool has(size_t count) const {
        return pos <= size && count <= size - pos;
    }
    bool u8(uint8_t& out) {
        if (!has(1)) return false;
        out = data[pos++];
        return true;
    }
    bool u16(uint16_t& out) {
        if (!has(2)) return false;
        memcpy(&out, data + pos, 2);
        pos += 2;
        return true;
    }
    bool u32(uint32_t& out) {
        if (!has(4)) return false;
        memcpy(&out, data + pos, 4);
        pos += 4;
        return true;
    }
    bool f32(float& out) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        memcpy(&out, &bits, 4);
        return true;
    }
    bool cstring(std::string& out) {
        if (!has(1)) return false;
        const uint8_t* end = (const uint8_t*)memchr(data + pos, 0, size - pos);
        if (!end) return false;
        out.assign((const char*)data + pos, (size_t)(end - (data + pos)));
        pos = (size_t)(end - data) + 1;
        return true;
    }
    bool skip(size_t count) {
        if (!has(count)) return false;
        pos += count;
        return true;
    }
};

}  // namespace mdl_detail
}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MDL_READER_H
