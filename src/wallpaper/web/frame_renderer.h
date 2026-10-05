#ifndef FRAME_RENDERER_H
#define FRAME_RENDERER_H

#include <string>

struct WebFrameBuffer;

namespace web_renderer {

// A web renderer backend. Implementations own their Qt objects and publish BGRA
// frames into the shared WebFrameBuffer. start() returns false when the backend
// cannot run, so the caller can fall back to another one.
class FrameRenderer {
   public:
    virtual ~FrameRenderer() = default;
    virtual bool start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) = 0;
};

}  // namespace web_renderer

#endif  // FRAME_RENDERER_H
