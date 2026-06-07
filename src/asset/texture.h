#ifndef TEXTURE_H
#define TEXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "../../libs/sokol/sokol_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
    int channels;
    sg_pixel_format format;
} DecodedTexture;

DecodedTexture load_texture(const char* path);
void free_texture(DecodedTexture tex);
void save_texture_as_png(DecodedTexture tex, const char* path);

#ifdef __cplusplus
}
#endif

#endif  // TEXTURE_H
