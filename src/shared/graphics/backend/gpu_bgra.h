#ifndef GPU_BGRA_H
#define GPU_BGRA_H

#include <cstdint>

#include <vulkan/vulkan.h>

#include "sokol_gfx.h"

// A BGRA8 DMA-BUF (e.g. an offscreen web frame) imported onto the engine's
// Vulkan device and sampled through a plain sampler.
struct ImportedBgraSurface {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Initializes the plain-sampler blit pipeline for BGRA DMA-BUF surfaces.
bool gpu_init_zero_copy_bgra();

// Imports a linear BGRA8 DMA-BUF fd onto the engine device. The caller keeps
// ownership of fd and may close it once this returns.
bool gpu_import_bgra_dmabuf(int fd, uint32_t width, uint32_t height, ImportedBgraSurface& out);

// Blits an imported BGRA surface into a Sokol image (0 CPU copies).
bool gpu_blit_zero_copy_bgra(const ImportedBgraSurface& surface, sg_image dst_image, int width, int height);

void gpu_destroy_bgra_surface(ImportedBgraSurface& surface);

#endif  // GPU_BGRA_H
