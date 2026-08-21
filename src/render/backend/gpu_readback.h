#ifndef GPU_READBACK_H
#define GPU_READBACK_H

#include <stdint.h>

#include <vector>

#include "sokol_gfx.h"

struct GpuImageReadbackResult {
    bool success = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba_data;
};

// Reads back RGBA8 pixels from an sg_image. Synchronous, intended for single-frame diagnostic captures.
GpuImageReadbackResult gpu_readback_image_rgba8(sg_image image);

#endif  // GPU_READBACK_H
