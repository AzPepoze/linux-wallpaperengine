#include "wallpaper/web/web_renderer_shared.h"

#include <string.h>

namespace web_renderer {

namespace {

// @FPS@ and @USER_PROPS@ are substituted by buildShimScript.
const char* kShimTemplate = R"JS(
(function () {
    if (window.__lweShimInstalled) return;
    window.__lweShimInstalled = true;

    window.wallpaperPropertyListener = window.wallpaperPropertyListener || {};
    window.__lweAudioCallback = null;
    window.wallpaperRegisterAudioListener = function (cb) {
        window.__lweAudioCallback = (typeof cb === 'function') ? cb : null;
    };
    setInterval(function () {
        if (window.__lweAudioCallback) window.__lweAudioCallback(new Float32Array(128));
    }, 33);

    var noop = function () {};
    window.wallpaperRegisterMediaStatusListener = window.wallpaperRegisterMediaStatusListener || noop;
    window.wallpaperRegisterMediaPropertiesListener = window.wallpaperRegisterMediaPropertiesListener || noop;
    window.wallpaperRegisterMediaThumbnailListener = window.wallpaperRegisterMediaThumbnailListener || noop;
    window.wallpaperRegisterMediaPlaybackListener = window.wallpaperRegisterMediaPlaybackListener || noop;
    window.wallpaperRegisterMediaTimelineListener = window.wallpaperRegisterMediaTimelineListener || noop;

    window.__lweApplyUserProperties = function (props) {
        var l = window.wallpaperPropertyListener;
        if (l && typeof l.applyUserProperties === 'function') l.applyUserProperties(props);
    };
    window.__lweApplyGeneralProperties = function (props) {
        var l = window.wallpaperPropertyListener;
        if (l && typeof l.applyGeneralProperties === 'function') l.applyGeneralProperties(props);
    };

    function applyDefaults() {
        window.__lweApplyGeneralProperties({"fps":@FPS@});
        window.__lweApplyUserProperties(@USER_PROPS@);
    }
    document.addEventListener('DOMContentLoaded', function () { setTimeout(applyDefaults, 150); });
    window.addEventListener('load', function () { setTimeout(applyDefaults, 150); });
})();
)JS";

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

}  // namespace

std::string buildShimScript(const std::string& user_properties_json, int fps) {
    std::string script = kShimTemplate;
    replaceAll(script, "@FPS@", std::to_string(fps));
    replaceAll(script, "@USER_PROPS@", user_properties_json.empty() ? "{}" : user_properties_json);
    return script;
}

RenderEvent decodeInput(const WebInputMessage& msg) {
    RenderEvent event;
    event.x = msg.x;
    event.y = msg.y;
    event.scroll_x = msg.scroll_x;
    event.scroll_y = msg.scroll_y;
    event.button = msg.button;
    event.modifiers = msg.modifiers;

    switch (msg.type) {
        case WEB_INPUT_MOUSE_MOVE:
            event.kind = EventKind::MouseMove;
            break;
        case WEB_INPUT_MOUSE_DOWN:
            event.kind = EventKind::MouseDown;
            break;
        case WEB_INPUT_MOUSE_UP:
            event.kind = EventKind::MouseUp;
            break;
        case WEB_INPUT_MOUSE_SCROLL:
            event.kind = EventKind::Scroll;
            break;
        case WEB_INPUT_SHUTDOWN:
            event.kind = EventKind::Shutdown;
            break;
        default:
            event.kind = EventKind::None;
            break;
    }
    return event;
}

bool publishFrame(WebFrameBuffer* frame, const uint8_t* bgra, uint32_t width, uint32_t height, bool flip_y) {
    if (!frame || !bgra || width == 0 || height == 0) return false;
    if (frame->width != width || frame->height != height) return false;

    uint8_t* pixels = reinterpret_cast<uint8_t*>(frame) + sizeof(WebFrameBuffer);
    const size_t stride = static_cast<size_t>(width) * 4;

    pthread_mutex_lock(&frame->mutex);
    for (uint32_t row = 0; row < height; ++row) {
        const uint32_t src_row = flip_y ? (height - 1 - row) : row;
        memcpy(pixels + static_cast<size_t>(row) * stride, bgra + static_cast<size_t>(src_row) * stride, stride);
    }
    ++frame->frame_counter;
    pthread_mutex_unlock(&frame->mutex);
    return true;
}

}  // namespace web_renderer
