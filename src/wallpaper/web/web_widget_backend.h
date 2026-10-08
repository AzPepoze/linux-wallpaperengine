#ifndef WEB_WIDGET_BACKEND_H
#define WEB_WIDGET_BACKEND_H

#include <stdint.h>

#include "wallpaper/web/frame_renderer.h"
#include "wallpaper/web/web_ipc.h"

class QTimer;
class QWebEngineView;
struct WebFrameBuffer;

namespace web_renderer {

// Fallback backend: a QWebEngineView widget under the offscreen QPA, captured with grab().
class WidgetBackend : public FrameRenderer {
   public:
    WidgetBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps);
    ~WidgetBackend() override;

    bool start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) override;

   private:
    void quit();
    void pollInput();
    void handleInput(const WebInputMessage& msg);
    void capture();

    WebFrameBuffer* frame_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fps_ = 60;
    QWebEngineView* view_ = nullptr;
    QTimer* render_timer_ = nullptr;
    int ctrl_fd_ = -1;
};

}  // namespace web_renderer

#endif  // WEB_WIDGET_BACKEND_H
