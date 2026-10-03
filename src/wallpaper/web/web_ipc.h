#ifndef WEB_IPC_H
#define WEB_IPC_H

// Shared-memory and control protocol between the engine process and the
// optional QtWebEngine renderer child. Deliberately free of Qt and Vulkan so
// the host translation unit has no Qt dependency.

#include <pthread.h>
#include <stdint.h>

// Header at the start of a memfd-backed region; the BGRA pixel payload of
// width * height * 4 bytes follows immediately after.
struct WebFrameBuffer {
    pthread_mutex_t mutex;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;  // 1 = BGRA8
    uint32_t reserved;
    uint64_t frame_counter;
};

enum WebInputType : uint32_t {
    WEB_INPUT_MOUSE_MOVE = 1,
    WEB_INPUT_MOUSE_DOWN = 2,
    WEB_INPUT_MOUSE_UP = 3,
    WEB_INPUT_MOUSE_SCROLL = 4,
    WEB_INPUT_KEY_DOWN = 5,
    WEB_INPUT_KEY_UP = 6,
    WEB_INPUT_SHUTDOWN = 7,
};

// sapp mouse buttons and key codes are forwarded verbatim; the renderer maps
// them to the Qt equivalents.
struct WebInputMessage {
    uint32_t type;
    float x;  // Normalized [0, 1] within the rendered frame.
    float y;
    float scroll_x;
    float scroll_y;
    uint32_t button;
    uint32_t key_code;
    uint32_t modifiers;
};

#endif  // WEB_IPC_H
