#ifndef FRAME_RENDERER_H
#define FRAME_RENDERER_H

#include <string>

struct WebFrameBuffer;

namespace web_renderer {

// A web renderer backend publishing BGRA frames into WebFrameBuffer; start() false means fall back.
class FrameRenderer {
   public:
    virtual ~FrameRenderer() = default;
    virtual bool start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) = 0;
};

}  // namespace web_renderer

#endif  // FRAME_RENDERER_H
