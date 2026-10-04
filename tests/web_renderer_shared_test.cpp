#include "wallpaper/web/web_ipc.h"
#include "wallpaper/web/web_renderer_shared.h"

#include <cstring>
#include <string>
#include <vector>

#include "test_util.h"

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

    return test::finish("web renderer shared checks");
}
