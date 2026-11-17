#include "app/platform/headless/headless_device.h"

#include <string.h>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "shared/graphics/backend/gpu_zero_copy.h"

namespace headless {
namespace {
constexpr const char* kTag = "HEADLESS";
const char* const kDeviceExtensions[] = {VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME};

// A device the engine can run on without presenting.
struct Candidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkPhysicalDeviceProperties properties = {};
    std::array<uint8_t, VK_UUID_SIZE> uuid = {};
};

bool hasDeviceExtensions(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());
    return std::all_of(std::begin(kDeviceExtensions), std::end(kDeviceExtensions), [&](const char* required) {
        return std::any_of(available.begin(), available.end(),
                           [&](const VkExtensionProperties& p) { return strcmp(p.extensionName, required) == 0; });
    });
}

bool findQueueFamily(VkPhysicalDevice device, uint32_t& family) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    for (uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & required) != required) continue;
        family = i;
        return true;
    }
    return false;
}

std::optional<Candidate> describeCandidate(VkPhysicalDevice device) {
    VkPhysicalDeviceIDProperties identity = {};
    identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties = {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &identity;
    vkGetPhysicalDeviceProperties2(device, &properties);
    if (properties.properties.apiVersion < VK_API_VERSION_1_3 || !hasDeviceExtensions(device)) return std::nullopt;

    Candidate candidate;
    if (!findQueueFamily(device, candidate.queue_family)) return std::nullopt;
    candidate.device = device;
    candidate.properties = properties.properties;
    memcpy(candidate.uuid.data(), identity.deviceUUID, VK_UUID_SIZE);
    return candidate;
}

// Same GPU the engine selected (--gpu or auto), then the first discrete GPU, then any usable one.
const Candidate* chooseCandidate(const std::vector<Candidate>& candidates) {
    const GpuDeviceManager& manager = GpuDeviceManager::instance();
    if (!manager.getDevices().empty()) {
        const auto& selected = manager.getSelectedGpu().device_uuid;
        for (const Candidate& candidate : candidates)
            if (candidate.uuid == selected) return &candidate;
        LOG_TAG_W(kTag, "selected GPU cannot run headless; picking another");
    }
    for (const Candidate& candidate : candidates)
        if (candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) return &candidate;
    return candidates.empty() ? nullptr : &candidates.front();
}
}  // namespace

std::unique_ptr<Device> Device::create() {
    std::unique_ptr<Device> self(new Device());
    if (!self->createInstance() || !self->pickPhysicalDevice() || !self->createDevice()) return nullptr;
    return self;
}

Device::~Device() {
    destroy();
}

bool Device::createInstance() {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "linux-wallpaperengine";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
        LOG_TAG_E(kTag, "vkCreateInstance failed");
        return false;
    }
    return true;
}

bool Device::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    std::vector<Candidate> candidates;
    for (VkPhysicalDevice device : devices)
        if (std::optional<Candidate> candidate = describeCandidate(device)) candidates.push_back(*candidate);

    const Candidate* chosen = chooseCandidate(candidates);
    if (!chosen) {
        LOG_TAG_E(kTag, "no Vulkan 1.3 GPU with the required extensions and queues");
        return false;
    }
    physical_device_ = chosen->device;
    queue_family_ = chosen->queue_family;
    LOG_TAG_I(kTag, "Vulkan device: %s", chosen->properties.deviceName);
    return true;
}

bool Device::createDevice() {
    const float priority = 0.0f;
    VkDeviceQueueCreateInfo queue = {};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = queue_family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan11Features supported_vk11 = {};
    supported_vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceFeatures2 supported = {};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supported.pNext = &supported_vk11;
    vkGetPhysicalDeviceFeatures2(physical_device_, &supported);

    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, available_extensions.data());
    std::vector<const char*> enabled_extensions(std::begin(kDeviceExtensions), std::end(kDeviceExtensions));
    const char* const video_extensions[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                                            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};
    const bool video_import_supported =
        supported_vk11.samplerYcbcrConversion &&
        std::all_of(std::begin(video_extensions), std::end(video_extensions), [&](const char* requested) {
            return std::any_of(available_extensions.begin(), available_extensions.end(),
                               [&](const VkExtensionProperties& extension) {
                                   return strcmp(extension.extensionName, requested) == 0;
                               });
        });
    if (video_import_supported) {
        enabled_extensions.insert(enabled_extensions.end(), std::begin(video_extensions), std::end(video_extensions));
    } else {
        LOG_TAG_I(kTag, "Vulkan DMA-BUF video import unavailable; video will use CPU transfer");
    }

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer = {};
    descriptor_buffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptor_buffer.descriptorBuffer = VK_TRUE;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    dynamic_state.pNext = &descriptor_buffer;
    dynamic_state.extendedDynamicState = VK_TRUE;
    VkPhysicalDeviceVulkan11Features vk11 = {};
    vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vk11.pNext = &dynamic_state;
    vk11.samplerYcbcrConversion = video_import_supported ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceVulkan12Features vk12 = {};
    vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12.pNext = &vk11;
    vk12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceVulkan13Features vk13 = {};
    vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13.pNext = &vk12;
    vk13.dynamicRendering = VK_TRUE;
    vk13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 required = {};
    required.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    required.pNext = &vk13;
    required.features.samplerAnisotropy = VK_TRUE;
    required.features.dualSrcBlend = VK_TRUE;
    required.features.textureCompressionBC = supported.features.textureCompressionBC;
    required.features.textureCompressionETC2 = supported.features.textureCompressionETC2;
    required.features.textureCompressionASTC_LDR = supported.features.textureCompressionASTC_LDR;

    VkDeviceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &required;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
    info.ppEnabledExtensionNames = enabled_extensions.data();
    if (vkCreateDevice(physical_device_, &info, nullptr, &device_) != VK_SUCCESS) {
        LOG_TAG_E(kTag, "vkCreateDevice failed");
        return false;
    }
    gpu_set_zero_copy_video_supported(video_import_supported);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return true;
}

sg_environment Device::environment() const {
    sg_environment env = {};
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
    env.defaults.sample_count = 1;
    env.vulkan.instance = instance_;
    env.vulkan.physical_device = physical_device_;
    env.vulkan.device = device_;
    env.vulkan.queue = queue_;
    env.vulkan.queue_family_index = queue_family_;
    return env;
}

void Device::destroy() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

}  // namespace headless
