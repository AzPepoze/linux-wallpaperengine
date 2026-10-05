#include "wallpaper/web/web_vulkan_backend.h"

#include <vulkan/vulkan.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QApplication>
#include <QQuickGraphicsConfiguration>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QVulkanInstance>
#include <memory>

#include "wallpaper/web/web_dmabuf_ipc.h"
#include "wallpaper/web/web_quick_view.h"
#include "wallpaper/web/web_renderer_shared.h"

namespace web_renderer {

struct ExportImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    int fd = -1;
};

namespace {

// DRM_FORMAT_ARGB8888; the byte layout of VK_FORMAT_B8G8R8A8_UNORM.
constexpr uint32_t kDrmFormatArgb8888 = 0x34325241;

bool createExportImage(VkDevice device, VkPhysicalDevice physical, uint32_t width, uint32_t height, ExportImage& out) {
    VkExternalMemoryImageCreateInfo external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.pNext = &external;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &image_info, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, out.image, &requirements);

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type = i;
            break;
        }
    }
    if (memory_type == UINT32_MAX) return false;

    VkExportMemoryAllocateInfo export_info = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.pNext = &export_info;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(device, &allocate_info, nullptr, &out.memory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(device, out.image, out.memory, 0) != VK_SUCCESS) return false;

    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
    if (!get_fd) return false;
    VkMemoryGetFdInfoKHR fd_info = {VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    fd_info.memory = out.memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    return get_fd(device, &fd_info, &out.fd) == VK_SUCCESS;
}

}  // namespace

struct VulkanBackend::Impl {
    QVulkanInstance vulkan;
    QQuickGraphicsConfiguration configuration;
    QQuickRenderControl control;
    QuickWebView view;
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QTimer> timer;

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    ExportImage images[kWebDmaBufBuffers];

    int ctrl_fd = -1;
    WebFrameBuffer* frame = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 60;

    Impl(WebFrameBuffer* buffer, uint32_t w, uint32_t h, uint32_t rate)
        : frame(buffer), width(w), height(h), fps(rate) {}

    ~Impl() {
        timer.reset();
        if (window) window->setRenderTarget(QQuickRenderTarget());
        window.reset();
        view = {};
        if (device != VK_NULL_HANDLE) {
            for (ExportImage& image : images) {
                if (image.fd >= 0) close(image.fd);
                if (image.image != VK_NULL_HANDLE) vkDestroyImage(device, image.image, nullptr);
                if (image.memory != VK_NULL_HANDLE) vkFreeMemory(device, image.memory, nullptr);
            }
        }
    }

    void tick();
    void quit();
    void pollInput();
};

void VulkanBackend::Impl::quit() {
    alarm(3);
    qApp->quit();
}

void VulkanBackend::Impl::pollInput() {
    if (ctrl_fd < 0) return;
    for (;;) {
        WebInputMessage msg;
        const ssize_t n = ::recv(ctrl_fd, &msg, sizeof(msg), MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            quit();
            return;
        }
        if (n == 0) {
            quit();
            return;
        }
        if (n != static_cast<ssize_t>(sizeof(msg))) continue;

        if (decodeInput(msg).kind == EventKind::Shutdown) {
            quit();
            return;
        }
        sendQuickInput(*window, msg, width, height);
    }
}

void VulkanBackend::Impl::tick() {
    pollInput();
    if (!window) return;

    const int slot = acquireDmaBuf(frame);
    window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(images[slot].image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                VK_FORMAT_B8G8R8A8_UNORM,
                                                                QSize(static_cast<int>(width), static_cast<int>(height))));
    renderQuickFrame(control);
    publishDmaBuf(frame, static_cast<uint32_t>(slot));
}

VulkanBackend::VulkanBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps)
    : impl_(new Impl(frame, width, height, fps)) {}

VulkanBackend::~VulkanBackend() {
    delete impl_;
}

bool VulkanBackend::start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) {
    Impl& d = *impl_;

    d.vulkan.setExtensions(QQuickGraphicsConfiguration::preferredInstanceExtensions());
    if (!d.vulkan.create()) return false;

    d.configuration.setDeviceExtensions({QByteArrayLiteral("VK_KHR_external_memory_fd"),
                                         QByteArrayLiteral("VK_EXT_external_memory_dma_buf")});

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    d.window = std::make_unique<QQuickWindow>(&d.control);
    d.window->setVulkanInstance(&d.vulkan);
    d.window->setGraphicsConfiguration(d.configuration);

    if (!d.control.initialize()) return false;

    auto* renderer = d.window->rendererInterface();
    auto* device = reinterpret_cast<const VkDevice*>(renderer->getResource(d.window.get(),
                                                                          QSGRendererInterface::DeviceResource));
    auto* physical = reinterpret_cast<const VkPhysicalDevice*>(
        renderer->getResource(d.window.get(), QSGRendererInterface::PhysicalDeviceResource));
    if (!device || !physical) return false;
    d.device = *device;
    d.physical = *physical;

    for (ExportImage& slot : d.images) {
        if (!createExportImage(d.device, d.physical, d.width, d.height, slot)) return false;
    }
    for (uint32_t i = 0; i < kWebDmaBufBuffers; ++i) {
        d.frame->buffers[i] = {};
        d.frame->buffers[i].fourcc = kDrmFormatArgb8888;
        d.frame->buffers[i].modifier = 0;  // linear
        d.frame->buffers[i].stride = d.width * 4;
        d.frame->buffers[i].offset = 0;
        d.frame->buffers[i].width = d.width;
        d.frame->buffers[i].height = d.height;
    }
    d.frame->transport = 1;
    d.frame->buffer_count = kWebDmaBufBuffers;

    d.ctrl_fd = ctrl_fd;
    if (ctrl_fd >= 0) {
        fcntl(ctrl_fd, F_SETFL, fcntl(ctrl_fd, F_GETFL, 0) | O_NONBLOCK);
        int fds[kWebDmaBufBuffers];
        for (uint32_t i = 0; i < kWebDmaBufBuffers; ++i) fds[i] = d.images[i].fd;
        if (!sendDmaBufOffer(ctrl_fd, fds, kWebDmaBufBuffers)) return false;
    }

    d.window->resize(static_cast<int>(d.width), static_cast<int>(d.height));
    d.view = createQuickWebView(*d.window, html_path, user_properties_json, static_cast<int>(d.fps));
    if (!d.view.item) return false;

    d.timer = std::make_unique<QTimer>();
    d.timer->setInterval(static_cast<int>(1000 / (d.fps > 0 ? d.fps : 60)));
    QObject::connect(d.timer.get(), &QTimer::timeout, [&d]() { d.tick(); });
    d.timer->start();
    return true;
}

}  // namespace web_renderer
