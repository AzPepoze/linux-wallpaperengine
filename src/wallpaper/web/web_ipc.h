#ifndef WEB_IPC_H
#define WEB_IPC_H

// Shared-memory and control protocol with the optional Qt renderer; kept free of Qt and Vulkan.

#include <pthread.h>
#include <stdint.h>

#include <atomic>

// A frame the helper exported as a DMA-BUF (tier B / zero-copy). The fds are
// transferred separately with SCM_RIGHTS; this is only the image metadata.
struct WebDmaBufBuffer {
    uint32_t fourcc;    // DRM_FORMAT_* of the image
    uint64_t modifier;  // DRM format modifier (0 = linear)
    uint32_t stride;
    uint32_t offset;
    uint32_t width;
    uint32_t height;
};

// Number of DMA-BUF buffers the helper cycles through.
constexpr uint32_t kWebDmaBufBuffers = 3;

// Sent helper -> engine over the control socket, with the fds attached as
// SCM_RIGHTS ancillary data (one per ring slot, in order).
struct WebDmaBufOffer {
    uint32_t type;   // WEB_MSG_DMABUF_OFFER
    uint32_t count;  // number of attached fds
};

// Header at the start of a memfd-backed region; the BGRA pixel payload of
// width * height * 4 bytes follows immediately after.
struct WebFrameBuffer {
    pthread_mutex_t mutex;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;  // 1 = BGRA8
    uint32_t reserved;
    uint64_t frame_counter;

    // Zero-copy transport. transport is 0 for shm pixels and 1 for the DMA-BUF
    // ring below; the pixel payload is unused when it is 1.
    uint32_t transport;
    uint32_t buffer_count;
    WebDmaBufBuffer buffers[kWebDmaBufBuffers];
    std::atomic<uint64_t> published_frame;  // frames the helper finished writing
    std::atomic<uint32_t> published_index;  // ring slot holding the newest frame
    std::atomic<uint64_t> consumed_frame;   // frames the engine finished sampling
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "shared atomics must be lock-free");

enum WebInputType : uint32_t {
    WEB_INPUT_MOUSE_MOVE = 1,
    WEB_INPUT_MOUSE_DOWN = 2,
    WEB_INPUT_MOUSE_UP = 3,
    WEB_INPUT_MOUSE_SCROLL = 4,
    WEB_INPUT_KEY_DOWN = 5,
    WEB_INPUT_KEY_UP = 6,
    WEB_INPUT_SHUTDOWN = 7,
};

enum WebMessageType : uint32_t {
    WEB_MSG_DMABUF_OFFER = 100,
};

// Input is forwarded verbatim; the renderer maps sapp codes to Qt.
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
