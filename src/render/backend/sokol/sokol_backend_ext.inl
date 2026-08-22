#include <string.h>

#include "../gpu_debug_labels.h"
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

void gpu_set_image_debug_label(sg_image image, const char* name) {
    if (!_sg.vk.ext.set_debug_utils_object_name_ext || !name || image.id == SG_INVALID_ID) return;
    _sg_image_t* img = _sg_lookup_image(image.id);
    if (!img || !img->vk.img) return;

    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = VK_OBJECT_TYPE_IMAGE;
    info.objectHandle = (uint64_t)img->vk.img;
    info.pObjectName = name;
    _sg.vk.ext.set_debug_utils_object_name_ext(_sg.vk.dev, &info);
}

void gpu_set_pipeline_debug_label(sg_pipeline pipeline, const char* name) {
    if (!_sg.vk.ext.set_debug_utils_object_name_ext || !name || pipeline.id == SG_INVALID_ID) return;
    _sg_pipeline_t* pip = _sg_lookup_pipeline(pipeline.id);
    if (!pip || !pip->vk.pip) return;

    VkDebugUtilsObjectNameInfoEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = VK_OBJECT_TYPE_PIPELINE;
    info.objectHandle = (uint64_t)pip->vk.pip;
    info.pObjectName = name;
    _sg.vk.ext.set_debug_utils_object_name_ext(_sg.vk.dev, &info);
}

#include <unordered_map>

#include "../gpu_zero_copy.h"
#include "formats/wallpaper_engine/texture/video_import_cache.h"

namespace {

const uint32_t kFullscreenTriVertSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000035, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0008000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x0000001d, 0x0000002c,
    0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005,
    0x00000009, 0x00736f70, 0x00060005, 0x0000000c, 0x565f6c67, 0x65747265, 0x646e4978, 0x00007865,
    0x00060005, 0x0000001b, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x0000001b,
    0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006, 0x0000001b, 0x00000001, 0x505f6c67,
    0x746e696f, 0x657a6953, 0x00000000, 0x00070006, 0x0000001b, 0x00000002, 0x435f6c67, 0x4470696c,
    0x61747369, 0x0065636e, 0x00070006, 0x0000001b, 0x00000003, 0x435f6c67, 0x446c6c75, 0x61747369,
    0x0065636e, 0x00030005, 0x0000001d, 0x00000000, 0x00050005, 0x0000002c, 0x65545f76, 0x6f6f4378,
    0x00006472, 0x00040047, 0x0000000c, 0x0000000b, 0x0000002a, 0x00030047, 0x0000001b, 0x00000002,
    0x00050048, 0x0000001b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000001b, 0x00000001,
    0x0000000b, 0x00000001, 0x00050048, 0x0000001b, 0x00000002, 0x0000000b, 0x00000003, 0x00050048,
    0x0000001b, 0x00000003, 0x0000000b, 0x00000004, 0x00040047, 0x0000002c, 0x0000001e, 0x00000000,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000002, 0x00040020, 0x00000008, 0x00000007, 0x00000007,
    0x00040015, 0x0000000a, 0x00000020, 0x00000001, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a,
    0x0004003b, 0x0000000b, 0x0000000c, 0x00000001, 0x0004002b, 0x0000000a, 0x0000000e, 0x00000001,
    0x0004002b, 0x0000000a, 0x00000010, 0x00000002, 0x00040017, 0x00000017, 0x00000006, 0x00000004,
    0x00040015, 0x00000018, 0x00000020, 0x00000000, 0x0004002b, 0x00000018, 0x00000019, 0x00000001,
    0x0004001c, 0x0000001a, 0x00000006, 0x00000019, 0x0006001e, 0x0000001b, 0x00000017, 0x00000006,
    0x0000001a, 0x0000001a, 0x00040020, 0x0000001c, 0x00000003, 0x0000001b, 0x0004003b, 0x0000001c,
    0x0000001d, 0x00000003, 0x0004002b, 0x0000000a, 0x0000001e, 0x00000000, 0x0004002b, 0x00000006,
    0x00000020, 0x40000000, 0x0004002b, 0x00000006, 0x00000022, 0x3f800000, 0x0004002b, 0x00000006,
    0x00000025, 0x00000000, 0x00040020, 0x00000029, 0x00000003, 0x00000017, 0x00040020, 0x0000002b,
    0x00000003, 0x00000007, 0x0004003b, 0x0000002b, 0x0000002c, 0x00000003, 0x0004002b, 0x00000018,
    0x0000002d, 0x00000000, 0x00040020, 0x0000002e, 0x00000007, 0x00000006, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003b, 0x00000008, 0x00000009,
    0x00000007, 0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c, 0x000500c4, 0x0000000a, 0x0000000f,
    0x0000000d, 0x0000000e, 0x000500c7, 0x0000000a, 0x00000011, 0x0000000f, 0x00000010, 0x0004006f,
    0x00000006, 0x00000012, 0x00000011, 0x0004003d, 0x0000000a, 0x00000013, 0x0000000c, 0x000500c7,
    0x0000000a, 0x00000014, 0x00000013, 0x00000010, 0x0004006f, 0x00000006, 0x00000015, 0x00000014,
    0x00050050, 0x00000007, 0x00000016, 0x00000012, 0x00000015, 0x0003003e, 0x00000009, 0x00000016,
    0x0004003d, 0x00000007, 0x0000001f, 0x00000009, 0x0005008e, 0x00000007, 0x00000021, 0x0000001f,
    0x00000020, 0x00050050, 0x00000007, 0x00000023, 0x00000022, 0x00000022, 0x00050083, 0x00000007,
    0x00000024, 0x00000021, 0x00000023, 0x00050051, 0x00000006, 0x00000026, 0x00000024, 0x00000000,
    0x00050051, 0x00000006, 0x00000027, 0x00000024, 0x00000001, 0x00070050, 0x00000017, 0x00000028,
    0x00000026, 0x00000027, 0x00000025, 0x00000022, 0x00050041, 0x00000029, 0x0000002a, 0x0000001d,
    0x0000001e, 0x0003003e, 0x0000002a, 0x00000028, 0x00050041, 0x0000002e, 0x0000002f, 0x00000009,
    0x0000002d, 0x0004003d, 0x00000006, 0x00000030, 0x0000002f, 0x00050041, 0x0000002e, 0x00000031,
    0x00000009, 0x00000019, 0x0004003d, 0x00000006, 0x00000032, 0x00000031, 0x00050083, 0x00000006,
    0x00000033, 0x00000022, 0x00000032, 0x00050050, 0x00000007, 0x00000034, 0x00000030, 0x00000033,
    0x0003003e, 0x0000002c, 0x00000034, 0x000100fd, 0x00010038};

const uint32_t kYcbcrBlitFragSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000014, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00000011, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00040005, 0x00000009, 0x6f435f6f, 0x00726f6c, 0x00060005, 0x0000000d, 0x69565f75,
    0x536f6564, 0x6c706d61, 0x00007265, 0x00050005, 0x00000011, 0x65545f76, 0x6f6f4378, 0x00006472,
    0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x00000021, 0x00000000,
    0x00040047, 0x0000000d, 0x00000022, 0x00000000, 0x00040047, 0x00000011, 0x0000001e, 0x00000000,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000a, 0x00000006, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x0000000b, 0x0000000a,
    0x00040020, 0x0000000c, 0x00000000, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000000,
    0x00040017, 0x0000000f, 0x00000006, 0x00000002, 0x00040020, 0x00000010, 0x00000001, 0x0000000f,
    0x0004003b, 0x00000010, 0x00000011, 0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000b, 0x0000000e, 0x0000000d, 0x0004003d,
    0x0000000f, 0x00000012, 0x00000011, 0x00050057, 0x00000007, 0x00000013, 0x0000000e, 0x00000012,
    0x0003003e, 0x00000009, 0x00000013, 0x000100fd, 0x00010038};

struct ZeroCopyState {
    VkSamplerYcbcrConversion ycbcr_conv = VK_NULL_HANDLE;
    VkSampler ycbcr_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkPipelineLayout pip_layout = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    struct FbEntry {
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer fb = VK_NULL_HANDLE;
    };
    std::unordered_map<uint32_t, FbEntry> fb_cache;
};

static ZeroCopyState s_zc;

}  // namespace

bool gpu_init_zero_copy_video(VideoImportCache& cache) {
    if (!_sg.vk.dev || !_sg.vk.phys_dev) return false;

    if (s_zc.ycbcr_conv == VK_NULL_HANDLE) {
        VkSamplerYcbcrConversionCreateInfo conv_info = {};
        conv_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        conv_info.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        conv_info.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
        conv_info.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        conv_info.xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_info.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_info.chromaFilter = VK_FILTER_LINEAR;
        conv_info.forceExplicitReconstruction = VK_FALSE;

        if (vkCreateSamplerYcbcrConversion(_sg.vk.dev, &conv_info, nullptr, &s_zc.ycbcr_conv) != VK_SUCCESS) {
            return false;
        }

        VkSamplerYcbcrConversionInfo sampler_conv_info = {};
        sampler_conv_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
        sampler_conv_info.conversion = s_zc.ycbcr_conv;

        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.pNext = &sampler_conv_info;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(_sg.vk.dev, &sampler_info, nullptr, &s_zc.ycbcr_sampler) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = &s_zc.ycbcr_sampler;

        VkDescriptorSetLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(_sg.vk.dev, &layout_info, nullptr, &s_zc.desc_layout) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128};
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 128;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;

        if (vkCreateDescriptorPool(_sg.vk.dev, &pool_info, nullptr, &s_zc.desc_pool) != VK_SUCCESS) {
            return false;
        }

        VkPipelineLayoutCreateInfo pip_layout_info = {};
        pip_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pip_layout_info.setLayoutCount = 1;
        pip_layout_info.pSetLayouts = &s_zc.desc_layout;

        if (vkCreatePipelineLayout(_sg.vk.dev, &pip_layout_info, nullptr, &s_zc.pip_layout) != VK_SUCCESS) {
            return false;
        }

        VkAttachmentDescription attachment = {};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference attachment_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &attachment_ref;

        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.attachmentCount = 1;
        rp_info.pAttachments = &attachment;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;

        if (vkCreateRenderPass(_sg.vk.dev, &rp_info, nullptr, &s_zc.render_pass) != VK_SUCCESS) {
            return false;
        }

        VkShaderModuleCreateInfo vert_info = {};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = sizeof(kFullscreenTriVertSpirv);
        vert_info.pCode = kFullscreenTriVertSpirv;
        VkShaderModule vert_mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(_sg.vk.dev, &vert_info, nullptr, &vert_mod) != VK_SUCCESS) return false;

        VkShaderModuleCreateInfo frag_info = {};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = sizeof(kYcbcrBlitFragSpirv);
        frag_info.pCode = kYcbcrBlitFragSpirv;
        VkShaderModule frag_mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(_sg.vk.dev, &frag_info, nullptr, &frag_mod) != VK_SUCCESS) {
            vkDestroyShaderModule(_sg.vk.dev, vert_mod, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert_mod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag_mod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi_state = {};
        vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia_state = {};
        ia_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp_state = {};
        vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp_state.viewportCount = 1;
        vp_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs_state = {};
        rs_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs_state.polygonMode = VK_POLYGON_MODE_FILL;
        rs_state.cullMode = VK_CULL_MODE_NONE;
        rs_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs_state.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms_state = {};
        ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cb_attach = {};
        cb_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT;
        cb_attach.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb_state = {};
        cb_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb_state.attachmentCount = 1;
        cb_state.pAttachments = &cb_attach;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn_state = {};
        dyn_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn_state.dynamicStateCount = 2;
        dyn_state.pDynamicStates = dyn_states;

        VkGraphicsPipelineCreateInfo pip_info = {};
        pip_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pip_info.stageCount = 2;
        pip_info.pStages = stages;
        pip_info.pVertexInputState = &vi_state;
        pip_info.pInputAssemblyState = &ia_state;
        pip_info.pViewportState = &vp_state;
        pip_info.pRasterizationState = &rs_state;
        pip_info.pMultisampleState = &ms_state;
        pip_info.pColorBlendState = &cb_state;
        pip_info.pDynamicState = &dyn_state;
        pip_info.layout = s_zc.pip_layout;
        pip_info.renderPass = s_zc.render_pass;

        VkResult pip_res =
            vkCreateGraphicsPipelines(_sg.vk.dev, VK_NULL_HANDLE, 1, &pip_info, nullptr, &s_zc.pipeline);
        vkDestroyShaderModule(_sg.vk.dev, vert_mod, nullptr);
        vkDestroyShaderModule(_sg.vk.dev, frag_mod, nullptr);
        if (pip_res != VK_SUCCESS) return false;
    }

    return cache.init(_sg.vk.dev, _sg.vk.phys_dev, s_zc.desc_pool, s_zc.desc_layout, s_zc.ycbcr_conv,
                      s_zc.ycbcr_sampler);
}

bool gpu_blit_zero_copy_surface(const ImportedVideoSurface& surface, sg_image dst_image, int width, int height) {
    if (!_sg.vk.dev || !_sg.vk.queue || dst_image.id == SG_INVALID_ID || surface.descriptor_set == VK_NULL_HANDLE ||
        s_zc.pipeline == VK_NULL_HANDLE) {
        return false;
    }

    _sg_image_t* dst = _sg_lookup_image(dst_image.id);
    if (!dst || !dst->vk.img) return false;

    auto it = s_zc.fb_cache.find(dst_image.id);
    if (it == s_zc.fb_cache.end()) {
        ZeroCopyState::FbEntry entry = {};
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = dst->vk.img;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(_sg.vk.dev, &view_info, nullptr, &entry.view) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = s_zc.render_pass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &entry.view;
        fb_info.width = (uint32_t)width;
        fb_info.height = (uint32_t)height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(_sg.vk.dev, &fb_info, nullptr, &entry.fb) != VK_SUCCESS) {
            vkDestroyImageView(_sg.vk.dev, entry.view, nullptr);
            return false;
        }
        it = s_zc.fb_cache.emplace(dst_image.id, entry).first;
    }

    VkCommandBuffer cmd = _sg_vk_staging_copy_begin();

    // Transition surface image to shader read optimal
    VkImageMemoryBarrier surface_barrier = {};
    surface_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    surface_barrier.srcAccessMask = 0;
    surface_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    surface_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    surface_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    surface_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    surface_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    surface_barrier.image = surface.image;
    surface_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // Transition destination image to color attachment optimal
    VkImageMemoryBarrier dst_barrier = {};
    dst_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_barrier.srcAccessMask = 0;
    dst_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dst_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dst_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    dst_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_barrier.image = dst->vk.img;
    dst_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier pre_barriers[2] = {surface_barrier, dst_barrier};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 2, pre_barriers);

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = s_zc.render_pass;
    rp_begin.framebuffer = it->second.fb;
    rp_begin.renderArea.extent = {(uint32_t)width, (uint32_t)height};

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {(uint32_t)width, (uint32_t)height}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_zc.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_zc.pip_layout, 0, 1, &surface.descriptor_set, 0,
                            nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Transition destination image to shader read optimal
    dst_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dst_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dst_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    dst_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dst_barrier);
    dst->vk.cur_access = _SG_VK_ACCESS_TEXTURE;

    _sg_vk_staging_copy_end(cmd, _sg.vk.queue);
    return true;
}
