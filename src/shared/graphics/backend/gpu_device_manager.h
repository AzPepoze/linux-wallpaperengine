#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct GpuDeviceInfo {
    uint32_t index = 0;
    std::array<uint8_t, VK_UUID_SIZE> device_uuid{};
    std::string name;
    std::string device_type;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    std::string pci_bus_id;
    std::string drm_render_node;
    std::string vaapi_driver;
    bool vaapi_supported = false;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
};

class GpuDeviceManager {
   public:
    static GpuDeviceManager& instance();

    void init();
    const std::vector<GpuDeviceInfo>& getDevices() const;
    bool selectGpu(const std::string& selector);
    const GpuDeviceInfo& getSelectedGpu() const;
    const std::string& getSelectedDrmRenderNode() const;
    void printGpuList() const;

   private:
    GpuDeviceManager();
    ~GpuDeviceManager();

    void probeDrmAndVaapi(GpuDeviceInfo& info);
    void applyEnvironmentVars(bool explicit_selection = false);

    VkInstance instance_ = VK_NULL_HANDLE;
    std::vector<GpuDeviceInfo> devices_;
    int selected_index_ = 0;
    bool initialized_ = false;
};
