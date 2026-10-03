#ifndef WAYLAND_VULKAN_SWAPCHAIN_H
#define WAYLAND_VULKAN_SWAPCHAIN_H

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "sokol_gfx.h"

struct wl_display;
struct wl_surface;

// Vulkan instance, device and swapchain for a Wayland surface, exposed to sokol_gfx the way sokol_app does.
class WaylandVulkanSwapchain {
   public:
    static std::unique_ptr<WaylandVulkanSwapchain> create(wl_display* display, wl_surface* surface, uint32_t width,
                                                          uint32_t height);
    ~WaylandVulkanSwapchain();
    WaylandVulkanSwapchain(const WaylandVulkanSwapchain&) = delete;
    WaylandVulkanSwapchain& operator=(const WaylandVulkanSwapchain&) = delete;

    sg_environment environment() const;
    bool resize(uint32_t width, uint32_t height);

    // Invalid when no image became available in time or the swapchain had to be rebuilt.
    sg_swapchain acquire();
    void present();

   private:
    WaylandVulkanSwapchain() = default;
    bool createInstance();
    bool pickPhysicalDevice(wl_display* display);
    bool createDevice();
    bool createSurface(wl_display* display, wl_surface* surface);
    bool createSwapchain();
    bool createDepth();
    bool createSync();
    void destroySwapchainResources();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format_ = {};
    VkExtent2D extent_ = {};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> present_complete_;
    std::vector<VkSemaphore> render_finished_;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    uint32_t sync_slot_ = 0;
    uint32_t image_index_ = 0;
    bool acquired_ = false;
};

#endif  // WAYLAND_VULKAN_SWAPCHAIN_H
