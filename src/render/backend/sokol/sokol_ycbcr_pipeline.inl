#pragma once

#include <unordered_map>

#include "../gpu_zero_copy.h"
#include "../shaders/fullscreen_tri.vert.spv.h"
#include "../shaders/ycbcr_blit.frag.spv.h"
#include "formats/wallpaper_engine/texture/video_import_cache.h"

namespace {

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
