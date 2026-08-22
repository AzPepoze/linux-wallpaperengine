#ifndef GPU_ZERO_COPY_H
#define GPU_ZERO_COPY_H

#include <cstdint>

#include "sokol_gfx.h"

class VideoImportCache;
struct ImportedVideoSurface;

// Initializes Vulkan YCbCr conversion, immutable sampler, descriptor layout, and blit pipeline for VideoImportCache
bool gpu_init_zero_copy_video(VideoImportCache& cache);

// Blits a hardware-decoded NV12 DMA-BUF surface directly into a Sokol RGBA8 image inside GPU VRAM (0 CPU copies)
bool gpu_blit_zero_copy_surface(const ImportedVideoSurface& surface, sg_image dst_image, int width, int height);

#endif  // GPU_ZERO_COPY_H
