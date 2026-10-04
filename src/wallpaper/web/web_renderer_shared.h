#ifndef WEB_RENDERER_SHARED_H
#define WEB_RENDERER_SHARED_H

#include <stdint.h>

#include <string>

#include "wallpaper/web/web_ipc.h"

// Qt-free pieces shared by the web renderer backends, so they can be unit
// tested without linking Qt.
namespace web_renderer {

// Builds the Wallpaper Engine API shim injected at DocumentCreation. The user
// properties JSON and the general properties (fps) are embedded and applied by
// the shim itself once the document is ready.
std::string buildShimScript(const std::string& user_properties_json, int fps);

// A control-socket message decoded into Qt-free values.
enum class EventKind { None, MouseMove, MouseDown, MouseUp, Scroll, Shutdown };

struct RenderEvent {
    EventKind kind = EventKind::None;
    float x = 0.0f;  // Normalised [0, 1] within the rendered frame.
    float y = 0.0f;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    uint32_t button = 0;
    uint32_t modifiers = 0;
};

RenderEvent decodeInput(const WebInputMessage& msg);

// Copies a BGRA8 frame into the shared buffer under its mutex and bumps the
// frame counter. flip_y reverses the row order (OpenGL readbacks are bottom-up).
// Returns false when the buffer or dimensions are unusable.
bool publishFrame(WebFrameBuffer* frame, const uint8_t* bgra, uint32_t width, uint32_t height, bool flip_y = false);

}  // namespace web_renderer

#endif  // WEB_RENDERER_SHARED_H
