#pragma once

#include <va/va.h>
#include <va/va_drmcommon.h>
#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

#include "video_types.h"

struct ImportedVideoSurface {
    VASurfaceID surface_id = VA_INVALID_SURFACE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t drm_fourcc = 0;
    uint64_t drm_modifier = 0;
    uint32_t num_planes = 0;
    uint32_t plane_offsets[4]{};
    uint32_t plane_pitches[4]{};
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

class VideoImportCache {
   public:
    VideoImportCache();
    ~VideoImportCache();

    bool init(VkDevice device, VkPhysicalDevice physical_device, VkDescriptorPool descriptor_pool,
              VkDescriptorSetLayout descriptor_layout, VkSamplerYcbcrConversion ycbcr_conversion,
              VkSampler video_sampler);

    ImportedVideoSurface* get_or_import(VADisplay display, VASurfaceID surface_id, int width, int height,
                                        ZeroCopyMetrics& zero_copy, PerformanceTiming& perf);

    void destroy();

    size_t get_active_imported_surfaces() const {
        return cache_.size();
    }
    uint64_t get_vk_images_created() const {
        return images_created_;
    }
    uint64_t get_vk_memory_allocations() const {
        return memory_allocs_;
    }
    uint64_t get_vk_image_views_created() const {
        return views_created_;
    }
    uint64_t get_descriptor_sets_created() const {
        return descriptor_sets_created_;
    }

   private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkSamplerYcbcrConversion ycbcr_conversion_ = VK_NULL_HANDLE;
    VkSampler video_sampler_ = VK_NULL_HANDLE;

    std::unordered_map<VASurfaceID, ImportedVideoSurface> cache_;

    uint64_t images_created_ = 0;
    uint64_t memory_allocs_ = 0;
    uint64_t views_created_ = 0;
    uint64_t descriptor_sets_created_ = 0;
};
