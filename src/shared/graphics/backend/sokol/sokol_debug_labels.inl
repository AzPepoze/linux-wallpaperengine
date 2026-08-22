#pragma once

#include "../gpu_debug_labels.h"

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
