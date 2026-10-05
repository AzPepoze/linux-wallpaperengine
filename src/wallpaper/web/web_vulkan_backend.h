#ifndef WEB_VULKAN_BACKEND_H
#define WEB_VULKAN_BACKEND_H

#include <stdint.h>

#include <string>

#include "wallpaper/web/frame_renderer.h"

struct WebFrameBuffer;

namespace web_renderer {

// Preferred backend: renders offscreen with Qt's Vulkan RHI into externally
// allocated BGRA8 images and exports each as a DMA-BUF the engine samples
// directly (no CPU copies). start() returns false when unavailable.
class VulkanBackend : public FrameRenderer {
   public:
    VulkanBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps);
    ~VulkanBackend() override;

    bool start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) override;

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace web_renderer

#endif  // WEB_VULKAN_BACKEND_H
