#ifndef WEB_RENDERER_SHARED_H
#define WEB_RENDERER_SHARED_H

#include <stdint.h>

#include <string>

#include "wallpaper/web/web_ipc.h"

// Qt-free helpers shared by the web renderer backends, testable without linking Qt.
namespace web_renderer {

// Builds the WE API shim; user properties and fps are embedded and applied once the document is ready.
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

// Copies a BGRA8 frame under the buffer mutex; flip_y fixes bottom-up GL readbacks.
bool publishFrame(WebFrameBuffer* frame, const uint8_t* bgra, uint32_t width, uint32_t height, bool flip_y = false);

// Zero-copy ring: the helper acquires a slot, renders, then publishes; acquireDmaBuf blocks while all slots are in flight.
int acquireDmaBuf(const WebFrameBuffer* frame);
void publishDmaBuf(WebFrameBuffer* frame, uint32_t index);

}  // namespace web_renderer

#endif  // WEB_RENDERER_SHARED_H
