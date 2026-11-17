#ifndef HEADLESS_DEVICE_H
#define HEADLESS_DEVICE_H

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

#include "sokol_gfx.h"

namespace headless {

// Vulkan instance and device without a window system, exposed to sokol_gfx.
class Device {
   public:
    // Returns nullptr when no usable GPU exists.
    static std::unique_ptr<Device> create();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    sg_environment environment() const;
    // Destroys the device and instance. Call after sg_shutdown.
    void destroy();

   private:
    Device() = default;
    bool createInstance();
    bool pickPhysicalDevice();
    bool createDevice();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
};

}  // namespace headless

#endif  // HEADLESS_DEVICE_H
