#pragma once

#include "../gpu_zero_copy.h"
#include "formats/wallpaper_engine/texture/video_import_cache.h"

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
