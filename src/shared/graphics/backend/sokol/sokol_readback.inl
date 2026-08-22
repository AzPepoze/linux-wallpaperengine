#pragma once

#include <cstring>

#include "../gpu_readback.h"

GpuImageReadbackResult gpu_readback_image_rgba8(sg_image image) {
    GpuImageReadbackResult result = {};
    if (image.id == SG_INVALID_ID) return result;

    _sg_image_t* img = _sg_lookup_image(image.id);
    if (!img || !img->vk.img || !_sg.vk.dev || !_sg.vk.queue) return result;

    const int width = img->cmn.width;
    const int height = img->cmn.height;
    if (width <= 0 || height <= 0) return result;

    const VkDeviceSize num_bytes = (VkDeviceSize)width * (VkDeviceSize)height * 4;

    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    VkBufferCreateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = num_bytes;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(_sg.vk.dev, &buf_info, nullptr, &staging_buf) != VK_SUCCESS) {
        return result;
    }

    VkMemoryRequirements mem_reqs = {};
    vkGetBufferMemoryRequirements(_sg.vk.dev, staging_buf, &mem_reqs);

    const int mem_type_idx = _sg_vk_mem_find_memory_type_index(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type_idx < 0) {
        vkDestroyBuffer(_sg.vk.dev, staging_buf, nullptr);
        return result;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = (uint32_t)mem_type_idx;

    if (vkAllocateMemory(_sg.vk.dev, &alloc_info, nullptr, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(_sg.vk.dev, staging_buf, nullptr);
        return result;
    }

    if (vkBindBufferMemory(_sg.vk.dev, staging_buf, staging_mem, 0) != VK_SUCCESS) {
        vkFreeMemory(_sg.vk.dev, staging_mem, nullptr);
        vkDestroyBuffer(_sg.vk.dev, staging_buf, nullptr);
        return result;
    }

    VkCommandBuffer cmd_buf = _sg_vk_staging_copy_begin();

    const _sg_vk_access_t orig_access = img->vk.cur_access;
    _sg_vk_image_barrier(cmd_buf, img, _SG_VK_ACCESS_STAGING);

    VkBufferImageCopy copy_region = {};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = (uint32_t)width;
    copy_region.bufferImageHeight = (uint32_t)height;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = {0, 0, 0};
    copy_region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};

    vkCmdCopyImageToBuffer(cmd_buf, img->vk.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buf, 1, &copy_region);

    _sg_vk_image_barrier(cmd_buf, img, orig_access);
    _sg_vk_staging_copy_end(cmd_buf, _sg.vk.queue);

    void* mapped_ptr = nullptr;
    if (vkMapMemory(_sg.vk.dev, staging_mem, 0, num_bytes, 0, &mapped_ptr) == VK_SUCCESS && mapped_ptr) {
        result.width = width;
        result.height = height;
        result.rgba_data.resize((size_t)num_bytes);
        memcpy(result.rgba_data.data(), mapped_ptr, (size_t)num_bytes);
        vkUnmapMemory(_sg.vk.dev, staging_mem);
        result.success = true;
    }

    vkFreeMemory(_sg.vk.dev, staging_mem, nullptr);
    vkDestroyBuffer(_sg.vk.dev, staging_buf, nullptr);

    return result;
}
