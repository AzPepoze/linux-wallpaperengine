#pragma once

#include "../gpu_bgra.h"

namespace {

struct BgraState {
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkPipelineLayout pip_layout = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    BlitFbCache fb_cache;
};

static BgraState s_bgra;

}  // namespace

bool gpu_init_zero_copy_bgra() {
    if (!_sg.vk.dev || !_sg.vk.phys_dev) return false;
    if (s_bgra.pipeline != VK_NULL_HANDLE) return true;

    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(_sg.vk.dev, &sampler_info, nullptr, &s_bgra.sampler) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(_sg.vk.dev, &layout_info, nullptr, &s_bgra.desc_layout) != VK_SUCCESS) return false;

    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 8;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(_sg.vk.dev, &pool_info, nullptr, &s_bgra.desc_pool) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pip_layout_info = {};
    pip_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pip_layout_info.setLayoutCount = 1;
    pip_layout_info.pSetLayouts = &s_bgra.desc_layout;
    if (vkCreatePipelineLayout(_sg.vk.dev, &pip_layout_info, nullptr, &s_bgra.pip_layout) != VK_SUCCESS) return false;

    if (!createBlitRenderPass(VK_FORMAT_B8G8R8A8_UNORM, s_bgra.render_pass)) return false;
    return createBlitPipeline(s_bgra.pip_layout, s_bgra.render_pass, s_bgra.pipeline);
}

bool gpu_import_bgra_dmabuf(int fd, uint32_t width, uint32_t height, ImportedBgraSurface& out) {
    if (!_sg.vk.dev || !_sg.vk.phys_dev || fd < 0 || width == 0 || height == 0 || s_bgra.desc_layout == VK_NULL_HANDLE)
        return false;

    VkExternalMemoryImageCreateInfo external_info = {};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(_sg.vk.dev, &image_info, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(_sg.vk.dev, out.image, &requirements);

    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(_sg.vk.phys_dev, &memory_properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if (!(requirements.memoryTypeBits & (1u << i))) continue;
        if (memory_type == UINT32_MAX) memory_type = i;
        if (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memory_type = i;
            break;
        }
    }
    if (memory_type == UINT32_MAX) {
        vkDestroyImage(_sg.vk.dev, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        return false;
    }

    VkImportMemoryFdInfoKHR import_info = {};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_info.fd = fd;

    VkMemoryDedicatedAllocateInfo dedicated_info = {};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext = &import_info;
    dedicated_info.image = out.image;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &dedicated_info;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(_sg.vk.dev, &alloc_info, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyImage(_sg.vk.dev, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindImageMemory(_sg.vk.dev, out.image, out.memory, 0) != VK_SUCCESS) return false;

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(_sg.vk.dev, &view_info, nullptr, &out.view) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo set_info = {};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = s_bgra.desc_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &s_bgra.desc_layout;
    if (vkAllocateDescriptorSets(_sg.vk.dev, &set_info, &out.descriptor_set) != VK_SUCCESS) return false;

    VkDescriptorImageInfo image_binding = {};
    image_binding.sampler = s_bgra.sampler;
    image_binding.imageView = out.view;
    image_binding.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = out.descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_binding;
    vkUpdateDescriptorSets(_sg.vk.dev, 1, &write, 0, nullptr);

    out.width = width;
    out.height = height;
    return true;
}

bool gpu_blit_zero_copy_bgra(const ImportedBgraSurface& surface, sg_image dst_image, int width, int height) {
    return blitImportedSurface(surface.image, surface.descriptor_set, dst_image, width, height, s_bgra.render_pass,
                               s_bgra.pipeline, s_bgra.pip_layout, s_bgra.fb_cache);
}

void gpu_destroy_bgra_surface(ImportedBgraSurface& surface) {
    if (!_sg.vk.dev) return;
    if (surface.descriptor_set != VK_NULL_HANDLE && s_bgra.desc_pool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(_sg.vk.dev, s_bgra.desc_pool, 1, &surface.descriptor_set);
    }
    if (surface.view != VK_NULL_HANDLE) vkDestroyImageView(_sg.vk.dev, surface.view, nullptr);
    if (surface.image != VK_NULL_HANDLE) vkDestroyImage(_sg.vk.dev, surface.image, nullptr);
    if (surface.memory != VK_NULL_HANDLE) vkFreeMemory(_sg.vk.dev, surface.memory, nullptr);
    surface = {};
}
