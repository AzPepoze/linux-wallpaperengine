from pathlib import Path

p = Path("src/render/backend/sokol/sokol_implementation.cpp")
s = p.read_text()
s = s.replace('#include <algorithm>\n#include <string>\n', '#include <algorithm>\n#include <cstring>\n#include <string>\n')

needle = '''    std::vector<VkDeviceQueueCreateInfo> queues(create_info->pQueueCreateInfos,\n                                                 create_info->pQueueCreateInfos + create_info->queueCreateInfoCount);\n'''
insert = '''    // libplacebo requires the Vulkan 1.2 timeline-semaphore and host-query-reset\n    // features when importing an existing VkDevice. Enable them in Sokol's\n    // existing Vulkan 1.2 feature chain when the physical device supports both.\n    VkPhysicalDeviceVulkan12Features supported_vk12 = {};\n    supported_vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;\n    VkPhysicalDeviceFeatures2 supported_features = {};\n    supported_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;\n    supported_features.pNext = &supported_vk12;\n    vkGetPhysicalDeviceFeatures2(physical_device, &supported_features);\n    if (supported_vk12.timelineSemaphore && supported_vk12.hostQueryReset) {\n        auto* chain = reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(create_info->pNext));\n        while (chain) {\n            if (chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {\n                auto* vk12 = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(chain);\n                vk12->timelineSemaphore = VK_TRUE;\n                vk12->hostQueryReset = VK_TRUE;\n                break;\n            }\n            chain = chain->pNext;\n        }\n    }\n\n    std::vector<VkDeviceQueueCreateInfo> queues(create_info->pQueueCreateInfos,\n                                                 create_info->pQueueCreateInfos + create_info->queueCreateInfoCount);\n'''
if needle not in s:
    raise RuntimeError("queue insertion point missing")
s = s.replace(needle, insert, 1)

needle = '''    if (video_family != VK_QUEUE_FAMILY_IGNORED) {\n        append_extension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);\n'''
insert = '''    // DMA-BUF import for the VA-API fallback stays on the same Sokol VkDevice.\n    // Enable only the external-memory extensions actually exposed by the driver.\n    const char* interop_extensions[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,\n                                        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,\n                                        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};\n    for (const char* extension : interop_extensions) {\n        if (lwe_vk_has_device_extension(physical_device, extension)) append_extension(extension);\n    }\n\n    if (video_family != VK_QUEUE_FAMILY_IGNORED) {\n        append_extension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);\n'''
if needle not in s:
    raise RuntimeError("extension insertion point missing")
s = s.replace(needle, insert, 1)
p.write_text(s)
