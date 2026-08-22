#include "gpu_device_manager.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <xf86drm.h>

#include <algorithm>
#include <cctype>

#include "shared/core/logger.h"

#define TAG "GPU"

namespace {

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

const char* deviceTypeToString(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU";
        default:
            return "Other";
    }
}

}  // namespace

GpuDeviceManager& GpuDeviceManager::instance() {
    static GpuDeviceManager mgr;
    return mgr;
}

GpuDeviceManager::GpuDeviceManager() = default;

GpuDeviceManager::~GpuDeviceManager() {
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void GpuDeviceManager::probeDrmAndVaapi(GpuDeviceInfo& info) {
    // Scan /dev/dri for render nodes
    DIR* dir = opendir("/dev/dri");
    if (!dir) return;

    struct dirent* entry = nullptr;
    std::vector<std::string> nodes;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "renderD", 7) == 0) {
            nodes.push_back(std::string("/dev/dri/") + entry->d_name);
        }
    }
    closedir(dir);
    std::sort(nodes.begin(), nodes.end());

    for (const auto& node_path : nodes) {
        int fd = open(node_path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        drmDevicePtr drm_dev = nullptr;
        if (drmGetDevice2(fd, 0, &drm_dev) == 0 && drm_dev) {
            bool matches = false;
            if (drm_dev->bustype == DRM_BUS_PCI) {
                char pci_buf[64] = {};
                snprintf(pci_buf, sizeof(pci_buf), "%04x:%02x:%02x.%d", drm_dev->businfo.pci->domain,
                         drm_dev->businfo.pci->bus, drm_dev->businfo.pci->dev, drm_dev->businfo.pci->func);
                if (info.pci_bus_id.empty() || info.pci_bus_id == pci_buf ||
                    (drm_dev->deviceinfo.pci->vendor_id == (uint16_t)info.vendor_id &&
                     drm_dev->deviceinfo.pci->device_id == (uint16_t)info.device_id)) {
                    matches = true;
                    if (info.pci_bus_id.empty()) info.pci_bus_id = pci_buf;
                }
            } else {
                matches = true;
            }

            if (matches) {
                info.drm_render_node = node_path;
                VADisplay va_disp = vaGetDisplayDRM(fd);
                if (va_disp) {
                    int major = 0;
                    int minor = 0;
                    if (vaInitialize(va_disp, &major, &minor) == VA_STATUS_SUCCESS) {
                        const char* driver_str = vaQueryVendorString(va_disp);
                        if (driver_str) info.vaapi_driver = driver_str;
                        info.vaapi_supported = true;
                        vaTerminate(va_disp);
                    }
                }
                drmFreeDevice(&drm_dev);
                close(fd);
                break;
            }
            drmFreeDevice(&drm_dev);
        }
        close(fd);
    }
}

void GpuDeviceManager::init() {
    if (initialized_) return;
    initialized_ = true;

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "GPU Device Manager";
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) {
        LOG_TAG_W(TAG, "Failed to create Vulkan instance for GPU enumeration");
        return;
    }

    uint32_t pdev_count = 0;
    if (vkEnumeratePhysicalDevices(instance_, &pdev_count, nullptr) != VK_SUCCESS || pdev_count == 0) {
        LOG_TAG_W(TAG, "No Vulkan physical devices found");
        return;
    }

    std::vector<VkPhysicalDevice> pdevs(pdev_count);
    vkEnumeratePhysicalDevices(instance_, &pdev_count, pdevs.data());

    for (uint32_t i = 0; i < pdev_count; ++i) {
        VkPhysicalDevice pdev = pdevs[i];

        VkPhysicalDevicePCIBusInfoPropertiesEXT pci_props = {};
        pci_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &pci_props;

        auto vkGetPhysicalDeviceProperties2Func =
            (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceProperties2");
        if (vkGetPhysicalDeviceProperties2Func) {
            vkGetPhysicalDeviceProperties2Func(pdev, &props2);
        } else {
            vkGetPhysicalDeviceProperties(pdev, &props2.properties);
        }

        GpuDeviceInfo info;
        info.index = i;
        info.name = props2.properties.deviceName;
        info.device_type = deviceTypeToString(props2.properties.deviceType);
        info.vendor_id = props2.properties.vendorID;
        info.device_id = props2.properties.deviceID;
        info.physical_device = pdev;

        if (pci_props.pciDomain != 0 || pci_props.pciBus != 0 || pci_props.pciDevice != 0 ||
            pci_props.pciFunction != 0) {
            char pci_str[64] = {};
            snprintf(pci_str, sizeof(pci_str), "%04x:%02x:%02x.%d", pci_props.pciDomain, pci_props.pciBus,
                     pci_props.pciDevice, pci_props.pciFunction);
            info.pci_bus_id = pci_str;
        }

        probeDrmAndVaapi(info);
        devices_.push_back(info);
    }

    // Default to discrete GPU if available, otherwise index 0
    selected_index_ = 0;
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].device_type == "Discrete GPU") {
            selected_index_ = (int)i;
            break;
        }
    }
}

const std::vector<GpuDeviceInfo>& GpuDeviceManager::getDevices() const {
    return devices_;
}

bool GpuDeviceManager::selectGpu(const std::string& selector) {
    if (devices_.empty()) return false;

    // Check if integer index
    char* endptr = nullptr;
    long index = strtol(selector.c_str(), &endptr, 10);
    if (*endptr == '\0' && index >= 0 && index < (long)devices_.size()) {
        selected_index_ = (int)index;
        return true;
    }

    std::string lower_sel = toLower(selector);

    // Match PCI ID
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (!devices_[i].pci_bus_id.empty() && toLower(devices_[i].pci_bus_id).find(lower_sel) != std::string::npos) {
            selected_index_ = (int)i;
            return true;
        }
    }

    // Match Name substring
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (toLower(devices_[i].name).find(lower_sel) != std::string::npos) {
            selected_index_ = (int)i;
            return true;
        }
    }

    LOG_TAG_W(TAG, "GPU selector '%s' did not match any available GPU", selector.c_str());
    return false;
}

const GpuDeviceInfo& GpuDeviceManager::getSelectedGpu() const {
    static GpuDeviceInfo empty;
    if (devices_.empty()) return empty;
    if (selected_index_ >= 0 && selected_index_ < (int)devices_.size()) {
        return devices_[selected_index_];
    }
    return devices_[0];
}

const std::string& GpuDeviceManager::getSelectedDrmRenderNode() const {
    const auto& gpu = getSelectedGpu();
    return gpu.drm_render_node;
}

void GpuDeviceManager::printGpuList() const {
    printf("Available GPUs:\n");
    printf("----------------------------------------------------------------------------------------------------\n");
    printf(" [%-3s] %-30s | %-14s | %-14s | %-18s | %s\n", "ID", "Device Name", "Type", "PCI Bus ID", "DRM Render Node",
           "VA-API HW Decode");
    printf("----------------------------------------------------------------------------------------------------\n");
    for (size_t i = 0; i < devices_.size(); ++i) {
        const auto& d = devices_[i];
        const char* sel = ((int)i == selected_index_) ? "*" : " ";
        printf("%s[%-3u] %-30s | %-14s | %-14s | %-18s | %s\n", sel, d.index, d.name.c_str(), d.device_type.c_str(),
               d.pci_bus_id.empty() ? "N/A" : d.pci_bus_id.c_str(),
               d.drm_render_node.empty() ? "N/A" : d.drm_render_node.c_str(),
               d.vaapi_supported ? "Supported" : "Not Available");
    }
    printf("----------------------------------------------------------------------------------------------------\n");
    printf(" * = Currently selected GPU\n");
}
