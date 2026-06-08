#include "texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libs/lz4.h"
#include "../core/logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../libs/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../libs/stb_image_write.h"

static uint32_t read_u32(FILE* f) {
    uint32_t val;
    if (fread(&val, 4, 1, f) != 1) return 0;
    return val;
}

static void read_string(FILE* f, char* buf, int n) {
    fread(buf, 1, n, f);
    buf[n] = '\0';
}

DecodedTexture load_texture(const char* path) {
    DecodedTexture tex = {0};
    tex.format = SG_PIXELFORMAT_RGBA8;

    const char* ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".tex") != 0) {
        tex.pixels = stbi_load(path, (int*)&tex.width, (int*)&tex.height, &tex.channels, 4);
        tex.channels = 4;
        tex.data_size = tex.width * tex.height * 4;
        tex.format = SG_PIXELFORMAT_RGBA8;
        return tex;
    }

    FILE* f = fopen(path, "rb");
    if (!f) return tex;

    char magic[9];
    read_string(f, magic, 8);
    fseek(f, 1, SEEK_CUR);  // skip null
    fseek(f, 8, SEEK_CUR);  // skip TEXI
    fseek(f, 1, SEEK_CUR);

    if (strcmp(magic, "TEXV0005") != 0) {
        LOG_E("Invalid .tex magic: %s", magic);
        fclose(f);
        return tex;
    }

    uint32_t wp_format = read_u32(f);
    fseek(f, 4, SEEK_CUR);
    uint32_t texW = read_u32(f);
    fseek(f, 4, SEEK_CUR);
    uint32_t imgW = read_u32(f);
    uint32_t imgH = read_u32(f);

    fseek(f, 4, SEEK_CUR);
    char container_magic[9];
    read_string(f, container_magic, 8);
    fseek(f, 1, SEEK_CUR);
    uint32_t image_count = read_u32(f);

    if (strcmp(container_magic, "TEXB0003") == 0) {
        read_u32(f);  // unknown
    }

    for (uint32_t i = 0; i < image_count; i++) {
        uint32_t mip_count = read_u32(f);
        for (uint32_t j = 0; j < mip_count; j++) {
            uint32_t mW = read_u32(f);
            uint32_t mH = read_u32(f);

            bool is_lz4 = false;
            uint32_t decomp_size = 0;

            if (strcmp(container_magic, "TEXB0001") != 0) {
                uint32_t flag = read_u32(f);
                is_lz4 = (flag == 1);
                decomp_size = read_u32(f);
            }

            uint32_t data_size = read_u32(f);
            if (i == 0 && j == 0) {
                uint8_t* data_block = malloc(data_size);
                if (!data_block) {
                    LOG_E("Failed to allocate memory for texture data block");
                    fclose(f);
                    return tex;
                }
                fread(data_block, 1, data_size, f);

                // Check for embedded PNG/JPG
                if (data_size > 8 && data_block[0] == 0x89 && data_block[1] == 'P' && data_block[2] == 'N' &&
                    data_block[3] == 'G') {
                    LOG_D("  Found embedded PNG in .tex");
                    int w, h, ch;
                    tex.pixels = stbi_load_from_memory(data_block, data_size, &w, &h, &ch, 4);
                    if (tex.pixels) {
                        tex.width = (uint32_t)w;
                        tex.height = (uint32_t)h;
                        tex.format = SG_PIXELFORMAT_RGBA8;
                        tex.data_size = (uint32_t)(w * h * 4);
                    }
                    free(data_block);
                    fclose(f);
                    return tex;
                }

                // Fallback to raw decoding
                uint8_t* raw_data = NULL;
                if (is_lz4) {
                    raw_data = malloc(decomp_size);
                    if (!raw_data) {
                        LOG_E("Failed to allocate memory for LZ4 decompression");
                        free(data_block);
                        fclose(f);
                        return tex;
                    }
                    LZ4_decompress_safe((char*)data_block, (char*)raw_data, data_size, decomp_size);
                } else {
                    raw_data = data_block;
                    decomp_size = data_size;
                }

                tex.width = imgW;
                tex.height = imgH;
                tex.data_size = decomp_size;

                switch (wp_format) {
                    case 0:
                        tex.format = SG_PIXELFORMAT_RGBA8;
                        tex.pixels = malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 4:
                        tex.format = SG_PIXELFORMAT_BC1_RGBA;
                        tex.pixels = malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 5:
                        tex.format = SG_PIXELFORMAT_BC2_RGBA;
                        tex.pixels = malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 6:
                        tex.format = SG_PIXELFORMAT_BC3_RGBA;
                        tex.pixels = malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 9:
                        tex.format = SG_PIXELFORMAT_RGBA8;
                        tex.pixels = malloc(imgW * imgH * 4);
                        if (tex.pixels) {
                            for (uint32_t k = 0; k < imgW * imgH; k++) {
                                uint8_t val = raw_data[k];
                                tex.pixels[k * 4 + 0] = val;
                                tex.pixels[k * 4 + 1] = val;
                                tex.pixels[k * 4 + 2] = val;
                                tex.pixels[k * 4 + 3] = 255;
                            }
                        }
                        tex.data_size = imgW * imgH * 4;
                        break;
                }

                if (is_lz4) free(raw_data);
                if (!is_lz4 || data_block != raw_data) free(data_block);
                fclose(f);
                return tex;
            } else {
                fseek(f, data_size, SEEK_CUR);
            }
        }
    }
    fclose(f);
    return tex;
}

void free_texture(DecodedTexture tex) {
    if (tex.pixels) free(tex.pixels);
}

void save_texture_as_png(DecodedTexture tex, const char* path) {
    if (tex.format == SG_PIXELFORMAT_RGBA8 && tex.pixels) {
        stbi_write_png(path, tex.width, tex.height, 4, tex.pixels, tex.width * 4);
    }
}
