#include "app/platform/wayland_layer/layer_app.h"

#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <atomic>

#include "app/platform/layer_options.h"
#include "app/frame_rate.h"
#include "app/platform/wayland_layer/wayland_layer_surface.h"
#include "app/platform/wayland_layer/wayland_vulkan_swapchain.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"

namespace {
constexpr double kMaxFrameSeconds = 0.1;
// linux/input-event-codes.h BTN_* values delivered by wl_pointer.button.
constexpr uint32_t kBtnLeft = 0x110;
constexpr uint32_t kBtnRight = 0x111;
constexpr uint32_t kBtnMiddle = 0x112;

double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

// linux/input-event-codes.h KEY_* values delivered by wl_keyboard.key, mapped to sokol key codes.
sapp_keycode sappKeycodeFromEvdev(uint32_t key) {
    if (key >= 2 && key <= 11) return (sapp_keycode)(SAPP_KEYCODE_1 + (key - 2));  // 1..0
    if (key >= 16 && key <= 25) return (sapp_keycode)(SAPP_KEYCODE_Q + (key - 16));
    if (key >= 30 && key <= 38) return (sapp_keycode)(SAPP_KEYCODE_A + (key - 30));
    if (key >= 44 && key <= 50) return (sapp_keycode)(SAPP_KEYCODE_Z + (key - 44));
    if (key >= 59 && key <= 68) return (sapp_keycode)(SAPP_KEYCODE_F1 + (key - 59));
    switch (key) {
        case 1: return SAPP_KEYCODE_ESCAPE;
        case 12: return SAPP_KEYCODE_MINUS;
        case 13: return SAPP_KEYCODE_EQUAL;
        case 14: return SAPP_KEYCODE_BACKSPACE;
        case 15: return SAPP_KEYCODE_TAB;
        case 26: return SAPP_KEYCODE_LEFT_BRACKET;
        case 27: return SAPP_KEYCODE_RIGHT_BRACKET;
        case 28: return SAPP_KEYCODE_ENTER;
        case 29: return SAPP_KEYCODE_LEFT_CONTROL;
        case 39: return SAPP_KEYCODE_SEMICOLON;
        case 40: return SAPP_KEYCODE_APOSTROPHE;
        case 41: return SAPP_KEYCODE_GRAVE_ACCENT;
        case 42: return SAPP_KEYCODE_LEFT_SHIFT;
        case 43: return SAPP_KEYCODE_BACKSLASH;
        case 51: return SAPP_KEYCODE_COMMA;
        case 52: return SAPP_KEYCODE_PERIOD;
        case 53: return SAPP_KEYCODE_SLASH;
        case 54: return SAPP_KEYCODE_RIGHT_SHIFT;
        case 56: return SAPP_KEYCODE_LEFT_ALT;
        case 57: return SAPP_KEYCODE_SPACE;
        case 58: return SAPP_KEYCODE_CAPS_LOCK;
        case 87: return SAPP_KEYCODE_F11;
        case 88: return SAPP_KEYCODE_F12;
        case 97: return SAPP_KEYCODE_RIGHT_CONTROL;
        case 100: return SAPP_KEYCODE_RIGHT_ALT;
        case 102: return SAPP_KEYCODE_HOME;
        case 103: return SAPP_KEYCODE_UP;
        case 104: return SAPP_KEYCODE_PAGE_UP;
        case 105: return SAPP_KEYCODE_LEFT;
        case 106: return SAPP_KEYCODE_RIGHT;
        case 107: return SAPP_KEYCODE_END;
        case 108: return SAPP_KEYCODE_DOWN;
        case 109: return SAPP_KEYCODE_PAGE_DOWN;
        case 110: return SAPP_KEYCODE_INSERT;
        case 111: return SAPP_KEYCODE_DELETE;
        case 125: return SAPP_KEYCODE_LEFT_SUPER;
        case 126: return SAPP_KEYCODE_RIGHT_SUPER;
        default: return SAPP_KEYCODE_INVALID;
    }
}

bool parseConfig(const CliOptions& cli, LayerSurfaceConfig& config) {
    config.output = cli.screen_root;
    if (!layer_options::parseLayer(cli.layer, config.layer)) {
        LOG_E("[LAYER] unknown --layer '%s' (use background, bottom, top or overlay)", cli.layer.c_str());
        return false;
    }
    if (!cli.layer_size.empty() && !layer_options::parseSize(cli.layer_size, config.width, config.height)) {
        LOG_E("[LAYER] invalid --layer-size '%s' (use WxH)", cli.layer_size.c_str());
        return false;
    }
    if (!cli.layer_anchor.empty() && !layer_options::parseAnchor(cli.layer_anchor, config.anchor)) {
        LOG_E("[LAYER] invalid --layer-anchor '%s'", cli.layer_anchor.c_str());
        return false;
    }
    return true;
}
}  // namespace

struct LayerApp::Impl : SurfaceProvider {
    std::unique_ptr<WaylandLayerSurface> wayland;
    std::unique_ptr<WaylandVulkanSwapchain> swapchain;
    std::atomic<bool> quit{false};
    double last_frame = 0.0;
    double frame_seconds = 1.0 / 60.0;
    float last_pointer_x = 0.0f;
    float last_pointer_y = 0.0f;

    int width() const override {
        return wayland->pixelWidth();
    }
    int height() const override {
        return wayland->pixelHeight();
    }
    float dpiScale() const override {
        return static_cast<float>(wayland->scale());
    }
    double frameDuration() const override {
        return frame_seconds;
    }
    void requestQuit() override {
        quit.store(true, std::memory_order_relaxed);
    }
    sg_environment environment() override {
        return swapchain->environment();
    }
    sg_swapchain acquireSwapchain() override {
        return swapchain->acquire();
    }

    void trackFrameTime();
    void forwardPointer(float x, float y, void (*event)(const sapp_event*));
    void forwardPointerButton(uint32_t button, bool pressed, void (*event)(const sapp_event*));
    void forwardPointerEnterLeave(bool entered, void (*event)(const sapp_event*));
    void forwardKey(uint32_t key, bool pressed, void (*event)(const sapp_event*));
    void forwardChar(uint32_t codepoint, void (*event)(const sapp_event*));
};

void LayerApp::Impl::trackFrameTime() {
    const double now = nowSeconds();
    if (last_frame > 0.0) frame_seconds = std::min(now - last_frame, kMaxFrameSeconds);
    last_frame = now;
}

void LayerApp::Impl::forwardPointer(float x, float y, void (*event)(const sapp_event*)) {
    last_pointer_x = x;
    last_pointer_y = y;
    sapp_event e = {};
    e.type = SAPP_EVENTTYPE_MOUSE_MOVE;
    e.mouse_x = x;
    e.mouse_y = y;
    event(&e);
}

void LayerApp::Impl::forwardPointerButton(uint32_t button, bool pressed, void (*event)(const sapp_event*)) {
    sapp_mousebutton code = SAPP_MOUSEBUTTON_INVALID;
    switch (button) {
        case kBtnLeft:
            code = SAPP_MOUSEBUTTON_LEFT;
            break;
        case kBtnRight:
            code = SAPP_MOUSEBUTTON_RIGHT;
            break;
        case kBtnMiddle:
            code = SAPP_MOUSEBUTTON_MIDDLE;
            break;
        default:
            return;
    }
    sapp_event e = {};
    e.type = pressed ? SAPP_EVENTTYPE_MOUSE_DOWN : SAPP_EVENTTYPE_MOUSE_UP;
    e.mouse_button = code;
    e.mouse_x = last_pointer_x;
    e.mouse_y = last_pointer_y;
    event(&e);
}

void LayerApp::Impl::forwardPointerEnterLeave(bool entered, void (*event)(const sapp_event*)) {
    sapp_event e = {};
    e.type = entered ? SAPP_EVENTTYPE_MOUSE_ENTER : SAPP_EVENTTYPE_MOUSE_LEAVE;
    e.mouse_x = last_pointer_x;
    e.mouse_y = last_pointer_y;
    event(&e);
}

void LayerApp::Impl::forwardKey(uint32_t key, bool pressed, void (*event)(const sapp_event*)) {
    const sapp_keycode code = sappKeycodeFromEvdev(key);
    if (code == SAPP_KEYCODE_INVALID) return;
    sapp_event e = {};
    e.type = pressed ? SAPP_EVENTTYPE_KEY_DOWN : SAPP_EVENTTYPE_KEY_UP;
    e.key_code = code;
    event(&e);
}

void LayerApp::Impl::forwardChar(uint32_t codepoint, void (*event)(const sapp_event*)) {
    sapp_event e = {};
    e.type = SAPP_EVENTTYPE_CHAR;
    e.char_code = codepoint;
    event(&e);
}

LayerApp::LayerApp(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

LayerApp::~LayerApp() = default;

std::unique_ptr<LayerApp> LayerApp::create(const CliOptions& cli) {
    if (!getenv("WAYLAND_DISPLAY")) {
        LOG_W("[LAYER] WAYLAND_DISPLAY is not set; running in a window");
        return nullptr;
    }
    LayerSurfaceConfig config;
    if (!parseConfig(cli, config)) return nullptr;

    auto impl = std::make_unique<Impl>();
    impl->wayland = WaylandLayerSurface::create(config);
    if (!impl->wayland) return nullptr;
    impl->swapchain = WaylandVulkanSwapchain::create(impl->wayland->display(), impl->wayland->surface(),
                                                     static_cast<uint32_t>(impl->wayland->pixelWidth()),
                                                     static_cast<uint32_t>(impl->wayland->pixelHeight()),
                                                     frame_rate::policyFor(cli.fps_limit).vsync);
    if (!impl->swapchain) return nullptr;
    return std::unique_ptr<LayerApp>(new LayerApp(std::move(impl)));
}

int LayerApp::run(const LayerAppCallbacks& callbacks) {
    Impl& app = *impl_;
    surface::setProvider(&app);
    app.wayland->setPointerHandler([&](float x, float y) { app.forwardPointer(x, y, callbacks.event); });
    app.wayland->setButtonHandler(
        [&](uint32_t button, bool pressed) { app.forwardPointerButton(button, pressed, callbacks.event); });
    app.wayland->setEnterLeaveHandler([&](bool entered) { app.forwardPointerEnterLeave(entered, callbacks.event); });
    app.wayland->setKeyHandler([&](uint32_t key, bool pressed) { app.forwardKey(key, pressed, callbacks.event); });
    app.wayland->setCharHandler([&](uint32_t codepoint) { app.forwardChar(codepoint, callbacks.event); });

    callbacks.init();
    while (!app.quit.load(std::memory_order_relaxed)) {
        if (!app.wayland->pump() || app.wayland->closed()) break;
        if (app.wayland->takeResize())
            app.swapchain->resize(static_cast<uint32_t>(app.wayland->pixelWidth()),
                                  static_cast<uint32_t>(app.wayland->pixelHeight()));
        app.trackFrameTime();
        callbacks.frame();
        app.swapchain->present();
    }
    if (app.wayland->closed()) LOG_I("[LAYER] the compositor closed the surface");
    callbacks.cleanup();
    surface::setProvider(nullptr);
    return 0;
}
