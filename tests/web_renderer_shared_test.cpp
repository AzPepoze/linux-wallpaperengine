#include "wallpaper/web/web_renderer_shared.h"

#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "test_util.h"
#include "wallpaper/web/web_dmabuf_ipc.h"
#include "wallpaper/web/web_ipc.h"
#include "wallpaper/web/web_transport.h"

int main() {
    // The shim embeds the properties and applies them itself.
    const std::string shim = web_renderer::buildShimScript("{\"a\":{\"value\":1}}", 30);
    CHECK(shim.find("wallpaperRegisterAudioListener") != std::string::npos);
    CHECK(shim.find("{\"a\":{\"value\":1}}") != std::string::npos);
    CHECK(shim.find("\"fps\":30") != std::string::npos);

    // Control-message decoding.
    WebInputMessage m = {};
    m.type = WEB_INPUT_MOUSE_MOVE;
    m.x = 0.25f;
    m.y = 0.5f;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::MouseMove);
    CHECK(web_renderer::decodeInput(m).x == 0.25f);
    CHECK(web_renderer::decodeInput(m).y == 0.5f);
    m.type = WEB_INPUT_MOUSE_DOWN;
    m.button = 1;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::MouseDown);
    CHECK(web_renderer::decodeInput(m).button == 1);
    m.type = WEB_INPUT_MOUSE_UP;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::MouseUp);
    m.type = WEB_INPUT_MOUSE_SCROLL;
    m.scroll_y = -1.0f;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::Scroll);
    CHECK(web_renderer::decodeInput(m).scroll_y == -1.0f);
    m.type = WEB_INPUT_SHUTDOWN;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::Shutdown);
    m.type = 999;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::None);

    // publishFrame copies the pixels under the mutex and bumps the counter.
    std::vector<uint8_t> storage(sizeof(WebFrameBuffer) + 16);
    std::memset(storage.data(), 0, storage.size());
    auto* fb = reinterpret_cast<WebFrameBuffer*>(storage.data());
    fb->width = 2;
    fb->height = 2;
    fb->pixel_format = 1;
    pthread_mutex_init(&fb->mutex, nullptr);
    std::vector<uint8_t> pixels(16, 0xAB);
    CHECK(web_renderer::publishFrame(fb, pixels.data(), 2, 2));
    CHECK(fb->frame_counter == 1);
    CHECK(reinterpret_cast<uint8_t*>(fb)[sizeof(WebFrameBuffer)] == 0xAB);
    CHECK(!web_renderer::publishFrame(fb, pixels.data(), 3, 2));  // wrong size -> rejected
    CHECK(fb->frame_counter == 1);
    pthread_mutex_destroy(&fb->mutex);

    // Zero-copy ring: acquire hands out successive slots and publish advances.
    std::vector<uint8_t> ring_storage(sizeof(WebFrameBuffer));
    std::memset(ring_storage.data(), 0, ring_storage.size());
    auto* ring = reinterpret_cast<WebFrameBuffer*>(ring_storage.data());
    new (&ring->published_frame) std::atomic<uint64_t>(0);
    new (&ring->published_index) std::atomic<uint32_t>(0);
    new (&ring->consumed_frame) std::atomic<uint64_t>(0);
    ring->buffer_count = kWebDmaBufBuffers;
    pthread_mutex_init(&ring->mutex, nullptr);

    CHECK(web_renderer::acquireDmaBuf(ring) == 0);
    web_renderer::publishDmaBuf(ring, 0);
    CHECK(ring->published_frame.load() == 1);
    CHECK(ring->published_index.load() == 0);
    CHECK(web_renderer::acquireDmaBuf(ring) == 1);

    // With all slots in flight, acquire blocks until the engine consumes one.
    web_renderer::publishDmaBuf(ring, 1);
    web_renderer::publishDmaBuf(ring, 2);
    ring->consumed_frame.store(1);
    CHECK(web_renderer::acquireDmaBuf(ring) == 0);

    pthread_mutex_destroy(&ring->mutex);

    // The DMA-BUF offer carries fds across the control socket.
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) == 0);
    const int sent_fd = memfd_create("lwe-test", 0);
    CHECK(sent_fd >= 0);
    CHECK(web_renderer::sendDmaBufOffer(sockets[0], &sent_fd, 1));
    int received[4] = {-1, -1, -1, -1};
    CHECK(web_renderer::receiveDmaBufOffer(sockets[1], received, 4) == 1);
    CHECK(received[0] >= 0);
    close(received[0]);
    close(sent_fd);
    close(sockets[0]);
    close(sockets[1]);

    // Transport names round-trip; anything else is rejected.
    WebTransport transport = WebTransport::Auto;
    CHECK(parseWebTransport("auto", transport) && transport == WebTransport::Auto);
    CHECK(parseWebTransport("dma-buf", transport) && transport == WebTransport::DmaBuf);
    CHECK(parseWebTransport("off-screen", transport) && transport == WebTransport::OffScreen);
    CHECK(parseWebTransport("snapshot", transport) && transport == WebTransport::Snapshot);
    CHECK(!parseWebTransport("widget", transport));
    CHECK(!parseWebTransport("bogus", transport));
    CHECK(strcmp(webTransportName(WebTransport::DmaBuf), "dma-buf") == 0);
    CHECK(strcmp(webTransportName(WebTransport::Auto), "auto") == 0);

    return test::finish("web renderer shared checks");
}
