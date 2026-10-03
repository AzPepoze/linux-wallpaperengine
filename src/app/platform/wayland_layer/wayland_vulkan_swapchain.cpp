#include "app/platform/wayland_layer/wayland_vulkan_swapchain.h"

#include <string.h>

#include <algorithm>

#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "shared/graphics/backend/gpu_zero_copy.h"

namespace {
constexpr uint64_t kAcquireTimeoutNs = 100ull * 1000 * 1000;
constexpr uint32_t kMinImageCount = 3;
const char* const kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME};

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

bool findQueueFamily(VkPhysicalDevice device, wl_display* display, uint32_t& family) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    for (uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & required) != required) continue;
        if (!vkGetPhysicalDeviceWaylandPresentationSupportKHR(device, i, display)) continue;
        family = i;
        return true;
    }
    return false;
}

VkSurfaceFormatKHR pickFormat(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());
    for (const VkSurfaceFormatKHR& format : formats)
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) return format;
    return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

sg_pixel_format toSokolFormat(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_UNORM ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_BGRA8;
}
}  // namespace

std::unique_ptr<WaylandVulkanSwapchain> WaylandVulkanSwapchain::create(wl_display* display, wl_surface* surface,
                                                                       uint32_t width, uint32_t height) {
    std::unique_ptr<WaylandVulkanSwapchain> self(new WaylandVulkanSwapchain());
    self->extent_ = {width, height};
    if (!self->createInstance() || !self->createSurface(display, surface) || !self->pickPhysicalDevice(display) ||
        !self->createDevice() || !self->createSwapchain())
        return nullptr;
    return self;
}

WaylandVulkanSwapchain::~WaylandVulkanSwapchain() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        destroySwapchainResources();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

bool WaylandVulkanSwapchain::createInstance() {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "linux-wallpaperengine";
    app.apiVersion = VK_API_VERSION_1_3;
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;
    if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
        LOG_E("[LAYER] vkCreateInstance failed (VK_KHR_wayland_surface unavailable?)");
        return false;
    }
    return true;
}

bool WaylandVulkanSwapchain::createSurface(wl_display* display, wl_surface* surface) {
    VkWaylandSurfaceCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    info.display = display;
    info.surface = surface;
    if (vkCreateWaylandSurfaceKHR(instance_, &info, nullptr, &surface_) != VK_SUCCESS) {
        LOG_E("[LAYER] vkCreateWaylandSurfaceKHR failed");
        return false;
    }
    return true;
}

bool WaylandVulkanSwapchain::pickPhysicalDevice(wl_display* display) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceIDProperties identity = {};
        identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &identity;
        vkGetPhysicalDeviceProperties2(device, &properties);
        const auto& selected = GpuDeviceManager::instance().getSelectedGpu();
        if (!std::equal(selected.device_uuid.begin(), selected.device_uuid.end(), identity.deviceUUID)) continue;
        const auto& props = properties.properties;
        if (props.apiVersion < VK_API_VERSION_1_3 || !hasDeviceExtensions(device)) continue;
        if (!findQueueFamily(device, display, queue_family_)) continue;
        physical_device_ = device;
        LOG_I("[LAYER] Vulkan device: %s", props.deviceName);
        return true;
    }
    LOG_E("[LAYER] selected GPU cannot present to the Wayland surface with the required Vulkan features");
    return false;
}

bool WaylandVulkanSwapchain::createDevice() {
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
        LOG_I("[LAYER] Vulkan DMA-BUF video import unavailable; video will use CPU transfer");
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
        LOG_E("[LAYER] vkCreateDevice failed");
        return false;
    }
    gpu_set_zero_copy_video_supported(video_import_supported);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return true;
}

bool WaylandVulkanSwapchain::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);
    // Wayland reports an undefined current extent, so the requested size decides.
    if (caps.currentExtent.width != 0xFFFFFFFFu) extent_ = caps.currentExtent;
    if (extent_.width == 0 || extent_.height == 0) return false;
    format_ = pickFormat(physical_device_, surface_);

    VkSwapchainCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = std::max(caps.minImageCount, kMinImageCount);
    if (caps.maxImageCount > 0) info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
    info.imageFormat = format_.format;
    info.imageColorSpace = format_.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_, &info, nullptr, &created) != VK_SUCCESS) {
        LOG_E("[LAYER] vkCreateSwapchainKHR failed");
        return false;
    }
    destroySwapchainResources();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = created;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    for (VkImage image : images_) {
        VkImageViewCreateInfo view = {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView created_view = VK_NULL_HANDLE;
        if (vkCreateImageView(device_, &view, nullptr, &created_view) != VK_SUCCESS) return false;
        views_.push_back(created_view);
    }
    return createDepth() && createSync();
}

bool WaylandVulkanSwapchain::createDepth() {
    VkImageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_D32_SFLOAT;
    info.extent = {extent_.width, extent_.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vkCreateImage(device_, &info, nullptr, &depth_image_) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, depth_image_, &requirements);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory);
    VkMemoryAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        const bool allowed = (requirements.memoryTypeBits & (1u << i)) != 0;
        if (allowed && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            alloc.memoryTypeIndex = i;
            break;
        }
    }
    if (alloc.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device_, &alloc, nullptr, &depth_memory_) != VK_SUCCESS)
        return false;
    vkBindImageMemory(device_, depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view = {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = depth_image_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_D32_SFLOAT;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    return vkCreateImageView(device_, &view, nullptr, &depth_view_) == VK_SUCCESS;
}

bool WaylandVulkanSwapchain::createSync() {
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (size_t i = 0; i < images_.size(); ++i) {
        VkSemaphore present_complete = VK_NULL_HANDLE;
        VkSemaphore render_finished = VK_NULL_HANDLE;
        if (vkCreateSemaphore(device_, &info, nullptr, &present_complete) != VK_SUCCESS) return false;
        present_complete_.push_back(present_complete);
        if (vkCreateSemaphore(device_, &info, nullptr, &render_finished) != VK_SUCCESS) return false;
        render_finished_.push_back(render_finished);
    }
    sync_slot_ = 0;
    acquired_ = false;
    return true;
}

void WaylandVulkanSwapchain::destroySwapchainResources() {
    for (VkSemaphore semaphore : present_complete_) vkDestroySemaphore(device_, semaphore, nullptr);
    for (VkSemaphore semaphore : render_finished_) vkDestroySemaphore(device_, semaphore, nullptr);
    for (VkImageView view : views_) vkDestroyImageView(device_, view, nullptr);
    if (depth_view_) vkDestroyImageView(device_, depth_view_, nullptr);
    if (depth_image_) vkDestroyImage(device_, depth_image_, nullptr);
    if (depth_memory_) vkFreeMemory(device_, depth_memory_, nullptr);
    present_complete_.clear();
    render_finished_.clear();
    views_.clear();
    images_.clear();
    depth_view_ = VK_NULL_HANDLE;
    depth_image_ = VK_NULL_HANDLE;
    depth_memory_ = VK_NULL_HANDLE;
}

sg_environment WaylandVulkanSwapchain::environment() const {
    sg_environment env = {};
    env.defaults.color_format = toSokolFormat(format_.format);
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
    env.defaults.sample_count = 1;
    env.vulkan.instance = instance_;
    env.vulkan.physical_device = physical_device_;
    env.vulkan.device = device_;
    env.vulkan.queue = queue_;
    env.vulkan.queue_family_index = queue_family_;
    return env;
}

bool WaylandVulkanSwapchain::resize(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(device_);
    extent_ = {width, height};
    acquired_ = false;
    return createSwapchain();
}

sg_swapchain WaylandVulkanSwapchain::acquire() {
    sg_swapchain result = {};
    result.invalid = true;
    if (!swapchain_) return result;
    const VkResult status = vkAcquireNextImageKHR(device_, swapchain_, kAcquireTimeoutNs, present_complete_[sync_slot_],
                                                  VK_NULL_HANDLE, &image_index_);
    if (status == VK_ERROR_OUT_OF_DATE_KHR) {
        resize(extent_.width, extent_.height);
        return result;
    }
    if (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) return result;
    acquired_ = true;
    result.invalid = false;
    result.width = static_cast<int>(extent_.width);
    result.height = static_cast<int>(extent_.height);
    result.sample_count = 1;
    result.color_format = toSokolFormat(format_.format);
    result.depth_format = SG_PIXELFORMAT_DEPTH;
    result.vulkan.render_image = images_[image_index_];
    result.vulkan.render_view = views_[image_index_];
    result.vulkan.depth_stencil_image = depth_image_;
    result.vulkan.depth_stencil_view = depth_view_;
    result.vulkan.render_finished_semaphore = render_finished_[image_index_];
    result.vulkan.present_complete_semaphore = present_complete_[sync_slot_];
    return result;
}

void WaylandVulkanSwapchain::present() {
    if (!acquired_) return;
    acquired_ = false;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_finished_[image_index_];
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &image_index_;
    const VkResult status = vkQueuePresentKHR(queue_, &info);
    sync_slot_ = (sync_slot_ + 1) % static_cast<uint32_t>(images_.size());
    if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR) {
        resize(extent_.width, extent_.height);
    } else if (status != VK_SUCCESS) {
        LOG_W("[LAYER] vkQueuePresentKHR failed (%d)", static_cast<int>(status));
    }
}
