#include "video_import_cache.h"

#include <drm_fourcc.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include "shared/core/logger.h"

#define TAG "CACHE"

VideoImportCache::VideoImportCache() = default;

VideoImportCache::~VideoImportCache() {
    destroy();
}

bool VideoImportCache::init(VkDevice device, VkPhysicalDevice physical_device, VkDescriptorPool descriptor_pool,
                            VkDescriptorSetLayout descriptor_layout, VkSamplerYcbcrConversion ycbcr_conversion,
                            VkSampler video_sampler) {
    device_ = device;
    physical_device_ = physical_device;
    descriptor_pool_ = descriptor_pool;
    descriptor_layout_ = descriptor_layout;
    ycbcr_conversion_ = ycbcr_conversion;
    video_sampler_ = video_sampler;
    return true;
}

ImportedVideoSurface* VideoImportCache::get_or_import(VADisplay display, VASurfaceID surface_id, int width, int height,
                                                      ZeroCopyMetrics& zero_copy, PerformanceTiming& perf) {
    auto t_cache_start = std::chrono::steady_clock::now();

    auto it = cache_.find(surface_id);
    if (it != cache_.end()) {
        ++zero_copy.import_cache_hits;
        auto t_cache_end = std::chrono::steady_clock::now();
        perf.import_cache_cpu_ms = std::chrono::duration<double, std::milli>(t_cache_end - t_cache_start).count();
        return &it->second;
    }

    ++zero_copy.import_cache_misses;

    auto t_export_start = std::chrono::steady_clock::now();
    VADRMPRIMESurfaceDescriptor descriptor{};
    VAStatus status =
        vaExportSurfaceHandle(display, surface_id, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                              VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS, &descriptor);
    auto t_export_end = std::chrono::steady_clock::now();
    perf.dmabuf_export_cpu_ms = std::chrono::duration<double, std::milli>(t_export_end - t_export_start).count();

    if (status != VA_STATUS_SUCCESS) {
        LOG_TAG_E(TAG, "vaExportSurfaceHandle failed: %s", vaErrorStr(status));
        return nullptr;
    }
    ++zero_copy.dmabuf_exports;

    if (descriptor.fourcc != DRM_FORMAT_NV12 || descriptor.num_objects != 1 || descriptor.num_layers != 1 ||
        descriptor.layers[0].num_planes != 2) {
        LOG_TAG_W(TAG,
                  "Unsupported DRM descriptor layout (fourcc=0x%x, objects=%u, layers=%u) — "
                  "zero-copy unavailable, falling back to CPU decode",
                  descriptor.fourcc, descriptor.num_objects, descriptor.num_layers);
        for (uint32_t o = 0; o < descriptor.num_objects; ++o) {
            close(descriptor.objects[o].fd);
        }
        return nullptr;
    }

    const auto& object = descriptor.objects[0];
    const auto& layer = descriptor.layers[0];
    int imported_fd = dup(object.fd);
    for (uint32_t o = 0; o < descriptor.num_objects; ++o) {
        close(descriptor.objects[o].fd);
    }
    if (imported_fd < 0) {
        LOG_TAG_E(TAG, "dup() failed for dma-buf fd");
        return nullptr;
    }

    VkSubresourceLayout plane_layouts[2]{};
    for (uint32_t plane = 0; plane < 2; ++plane) {
        plane_layouts[plane].offset = layer.offset[plane];
        plane_layouts[plane].rowPitch = layer.pitch[plane];
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info = {};
    modifier_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modifier_info.drmFormatModifier = object.drm_format_modifier;
    modifier_info.drmFormatModifierPlaneCount = 2;
    modifier_info.pPlaneLayouts = plane_layouts;

    VkExternalMemoryImageCreateInfo external_info = {};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.pNext = &modifier_info;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    image_info.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ImportedVideoSurface surface;
    surface.surface_id = surface_id;
    surface.width = static_cast<uint32_t>(width);
    surface.height = static_cast<uint32_t>(height);
    surface.drm_fourcc = descriptor.fourcc;
    surface.drm_modifier = object.drm_format_modifier;
    surface.num_planes = layer.num_planes;
    for (uint32_t p = 0; p < layer.num_planes; ++p) {
        surface.plane_offsets[p] = layer.offset[p];
        surface.plane_pitches[p] = layer.pitch[p];
    }
    surface.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = vkCreateImage(device_, &image_info, nullptr, &surface.image);
    if (res != VK_SUCCESS) {
        LOG_TAG_W(TAG,
                  "vkCreateImage failed (VkResult=%d) — DRM modifier 0x%llx likely unsupported on this GPU, "
                  "falling back to CPU decode",
                  res, (unsigned long long)object.drm_format_modifier);
        close(imported_fd);
        return nullptr;
    }
    ++images_created_;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, surface.image, &requirements);

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
    uint32_t memory_type = 0;
    while (memory_type < memory_properties.memoryTypeCount &&
           (!(requirements.memoryTypeBits & (1u << memory_type)) ||
            !(memory_properties.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) {
        ++memory_type;
    }

    VkImportMemoryFdInfoKHR import_info = {};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_info.fd = imported_fd;

    VkMemoryDedicatedAllocateInfo dedicated_info = {};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext = &import_info;
    dedicated_info.image = surface.image;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &dedicated_info;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = memory_type;

    res = vkAllocateMemory(device_, &alloc_info, nullptr, &surface.memory);
    if (res != VK_SUCCESS) {
        LOG_TAG_W(TAG,
                  "vkAllocateMemory failed (VkResult=%d) — cannot import cross-adapter DMA-BUF, "
                  "falling back to CPU decode",
                  res);
        vkDestroyImage(device_, surface.image, nullptr);
        close(imported_fd);
        return nullptr;
    }
    ++memory_allocs_;

    res = vkBindImageMemory(device_, surface.image, surface.memory, 0);
    if (res != VK_SUCCESS) {
        LOG_TAG_W(TAG,
                  "vkBindImageMemory failed (VkResult=%d) — DMA-BUF bind rejected, "
                  "falling back to CPU decode",
                  res);
        vkFreeMemory(device_, surface.memory, nullptr);
        vkDestroyImage(device_, surface.image, nullptr);
        return nullptr;
    }

    VkSamplerYcbcrConversionInfo view_conversion = {};
    view_conversion.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    view_conversion.conversion = ycbcr_conversion_;

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.pNext = ycbcr_conversion_ != VK_NULL_HANDLE ? &view_conversion : nullptr;
    view_info.image = surface.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    res = vkCreateImageView(device_, &view_info, nullptr, &surface.view);
    if (res != VK_SUCCESS) {
        LOG_TAG_E(TAG, "vkCreateImageView failed: %d", res);
        vkFreeMemory(device_, surface.memory, nullptr);
        vkDestroyImage(device_, surface.image, nullptr);
        return nullptr;
    }
    ++views_created_;

    if (descriptor_pool_ != VK_NULL_HANDLE && descriptor_layout_ != VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo set_info = {};
        set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_info.descriptorPool = descriptor_pool_;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &descriptor_layout_;

        res = vkAllocateDescriptorSets(device_, &set_info, &surface.descriptor_set);
        if (res == VK_SUCCESS) {
            ++descriptor_sets_created_;
            VkDescriptorImageInfo desc_image_info = {};
            desc_image_info.sampler = video_sampler_;
            desc_image_info.imageView = surface.view;
            desc_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = surface.descriptor_set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &desc_image_info;

            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    ++zero_copy.vulkan_imports_created;
    auto insert_result = cache_.emplace(surface_id, surface);

    auto t_cache_end = std::chrono::steady_clock::now();
    perf.import_cache_cpu_ms = std::chrono::duration<double, std::milli>(t_cache_end - t_cache_start).count();

    return &insert_result.first->second;
}

void VideoImportCache::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    for (auto& pair : cache_) {
        ImportedVideoSurface& surface = pair.second;
        if (surface.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, surface.view, nullptr);
            surface.view = VK_NULL_HANDLE;
        }
        if (surface.image != VK_NULL_HANDLE) {
            vkDestroyImage(device_, surface.image, nullptr);
            surface.image = VK_NULL_HANDLE;
        }
        if (surface.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, surface.memory, nullptr);
            surface.memory = VK_NULL_HANDLE;
        }
    }
    cache_.clear();
}
