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

#define TAG "TEXTURE"

static uint32_t read_u32(FILE* f) {
    uint32_t val;
    if (fread(&val, 4, 1, f) != 1) return 0;
    return val;
}

static void read_string(FILE* f, char* buf, int n) {
    fread(buf, 1, n, f);
    buf[n] = '\0';
}

DecodedTexture load_texture(const char* path, int image_index) {
    DecodedTexture tex = {0};
    tex.format = SG_PIXELFORMAT_RGBA8;

    const char* ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".tex") != 0) {
        if (image_index > 0) return tex;  // Only first image for non-.tex files
        LOG_TAG_D(TAG, "Loading image: %s", path);
        tex.pixels = (uint8_t*)stbi_load(path, (int*)&tex.width, (int*)&tex.height, &tex.channels, 4);
        if (!tex.pixels) {
            LOG_TAG_E(TAG, "Failed to load image: %s", path);
            return tex;
        }
        tex.channels = 4;
        tex.data_size = tex.width * tex.height * 4;
        tex.format = SG_PIXELFORMAT_RGBA8;
        LOG_TAG_I(TAG, "Loaded image: %s (%dx%d)", path, tex.width, tex.height);
        return tex;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_TAG_E(TAG, "Failed to open texture: %s", path);
        return tex;
    }

    char magic[9];
    read_string(f, magic, 8);
    fseek(f, 1, SEEK_CUR);  // skip null
    fseek(f, 8, SEEK_CUR);  // skip TEXI
    fseek(f, 1, SEEK_CUR);

    if (strncmp(magic, "TEXV", 4) != 0) {
        LOG_TAG_E(TAG, "Invalid .tex magic: %s in %s", magic, path);
        fclose(f);
        return tex;
    }
    LOG_TAG_D(TAG, "  .tex version: %s", magic);

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

    LOG_TAG_I(TAG, "Loading texture: %s (images: %u, requested: %d)", path, image_count, image_index);
    if (image_index >= (int)image_count) {
        LOG_TAG_W(TAG, "  Requested image index %d out of bounds (count: %u)", image_index, image_count);
        fclose(f);
        return tex;
    }

    const char* format_name = "Unknown";
    // ... rest of format detection ...
    switch (wp_format) {
        case 0:
            format_name = "RGBA8";
            break;
        case 4:
            format_name = "DXT1/BC1";
            break;
        case 5:
            format_name = "DXT3/BC2";
            break;
        case 6:
            format_name = "DXT5/BC3";
            break;
        case 9:
            format_name = "R8 (Grayscale)";
            break;
    }

    LOG_TAG_D(TAG, "  Format: %s (wp:%u), Size: %dx%d, Container: %s", format_name, wp_format, imgW, imgH,
              container_magic);

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
            if (i == (uint32_t)image_index && j == 0) {
                uint8_t* data_block = (uint8_t*)malloc(data_size);
                if (!data_block) {
                    LOG_TAG_E(TAG, "Failed to allocate memory for texture data block");
                    fclose(f);
                    return tex;
                }
                fread(data_block, 1, data_size, f);

                // Check for embedded PNG/JPG
                if (data_size > 8 && data_block[0] == 0x89 && data_block[1] == 'P' && data_block[2] == 'N' &&
                    data_block[3] == 'G') {
                    LOG_TAG_I(TAG, "  Found embedded PNG in .tex");
                    int w, h, ch;
                    tex.pixels = (uint8_t*)stbi_load_from_memory(data_block, data_size, &w, &h, &ch, 4);
                    if (tex.pixels) {
                        tex.width = (uint32_t)w;
                        tex.height = (uint32_t)h;
                        tex.format = SG_PIXELFORMAT_RGBA8;
                        tex.data_size = (uint32_t)(w * h * 4);
                        LOG_TAG_I(TAG, "  Successfully loaded embedded PNG (%dx%d)", w, h);
                    }
                    free(data_block);
                    fclose(f);
                    return tex;
                }

                // Fallback to raw decoding
                uint8_t* raw_data = NULL;
                if (is_lz4) {
                    LOG_TAG_D(TAG, "  Decompressing LZ4 (size: %u -> %u)", data_size, decomp_size);
                    raw_data = (uint8_t*)malloc(decomp_size);
                    if (!raw_data) {
                        LOG_TAG_E(TAG, "Failed to allocate memory for LZ4 decompression");
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
                        tex.pixels = (uint8_t*)malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 4:
                        tex.format = SG_PIXELFORMAT_BC1_RGBA;
                        tex.pixels = (uint8_t*)malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 5:
                        tex.format = SG_PIXELFORMAT_BC2_RGBA;
                        tex.pixels = (uint8_t*)malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 6:
                        tex.format = SG_PIXELFORMAT_BC3_RGBA;
                        tex.pixels = (uint8_t*)malloc(decomp_size);
                        if (tex.pixels) memcpy(tex.pixels, raw_data, decomp_size);
                        break;
                    case 9:
                        tex.format = SG_PIXELFORMAT_RGBA8;
                        tex.pixels = (uint8_t*)malloc(imgW * imgH * 4);
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

                if (tex.pixels) {
                    LOG_TAG_I(TAG, "  Successfully loaded texture (%dx%d, format: %s)", tex.width, tex.height,
                              format_name);
                } else {
                    LOG_TAG_E(TAG, "  Failed to allocate pixels for texture");
                }

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
        stbi_write_png(path, tex.width, tex.height, 4, (void*)tex.pixels, tex.width * 4);
    }
}
