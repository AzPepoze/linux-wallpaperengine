#ifndef WEB_RENDER_CONTROL_H
#define WEB_RENDER_CONTROL_H

#include <stdint.h>

#include <string>

#include "wallpaper/web/frame_renderer.h"

struct WebFrameBuffer;

namespace web_renderer {

// Preferred backend: renders the page offscreen with QQuickRenderControl + a
// QML WebEngineView into an OpenGL FBO and reads it back into the shared frame.
// start() returns false when no GL context or render control can be created, so
// the caller can fall back to the widget backend.
class RenderControlBackend : public FrameRenderer {
   public:
    RenderControlBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps);
    ~RenderControlBackend() override;

    bool start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) override;

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace web_renderer

#endif  // WEB_RENDER_CONTROL_H
