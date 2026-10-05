#include "app/platform/wayland_layer/wayland_layer_surface.h"

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>

#include "shared/core/build_config.h"
#include "shared/core/logger.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#ifdef LWE_HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

namespace {
constexpr uint32_t kMaxOutputVersion = 4;
constexpr uint32_t kMaxSeatVersion = 5;
constexpr const char* kNamespace = "linux-wallpaperengine";
}  // namespace

struct WaylandLayerSurface::Impl {
    struct Output {
        Impl* owner = nullptr;
        wl_output* proxy = nullptr;
        std::string name;
        int scale = 1;
    };

    LayerSurfaceConfig config;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer_surface = nullptr;
    std::vector<std::unique_ptr<Output>> outputs;
    Output* target = nullptr;
    int width = 0;
    int height = 0;
    int scale = 1;
    bool configured = false;
    bool closed = false;
    bool resized = false;
    PointerHandler pointer_handler;
    ButtonHandler button_handler;
    EnterLeaveHandler enter_leave_handler;
    KeyHandler key_handler;
    CharHandler char_handler;
#ifdef LWE_HAVE_XKBCOMMON
    xkb_context* xkb_ctx = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* kb_state = nullptr;
#endif

    ~Impl();
    bool connect();
    bool chooseOutput();
    bool createSurface();
    void applyScale();
    void onConfigure(uint32_t serial, uint32_t w, uint32_t h);
    void onPointerMotion(wl_fixed_t x, wl_fixed_t y);
    void onPointerButton(uint32_t button, uint32_t state);
    void onPointerEnterLeave(bool entered);
    void onKey(uint32_t key, uint32_t state);
    void onModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void updateSeat(uint32_t capabilities);

    static void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                               uint32_t version);
    static void registryRemove(void*, wl_registry*, uint32_t) {}
    static void outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                               int32_t) {}
    static void outputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
    static void outputDone(void*, wl_output*) {}
    static void outputScale(void* data, wl_output*, int32_t factor);
    static void outputName(void* data, wl_output*, const char* name);
    static void outputDescription(void*, wl_output*, const char*) {}
    static void seatCapabilities(void* data, wl_seat*, uint32_t capabilities);
    static void seatName(void*, wl_seat*, const char*) {}
    static void pointerEnter(void* data, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t x, wl_fixed_t y);
    static void pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*);
    static void pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y);
    static void pointerButton(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state);
    static void pointerAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
    static void pointerFrame(void*, wl_pointer*) {}
    static void pointerAxisSource(void*, wl_pointer*, uint32_t) {}
    static void pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
    static void pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
    static void keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size);
    static void keyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
    static void keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
    static void keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state);
    static void keyboardModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched,
                                  uint32_t locked, uint32_t group);
    static void keyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}
    static void layerConfigure(void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h);
    static void layerClosed(void* data, zwlr_layer_surface_v1*);
};

namespace {
const wl_registry_listener kRegistryListener = {WaylandLayerSurface::Impl::registryGlobal,
                                                WaylandLayerSurface::Impl::registryRemove};
const wl_output_listener kOutputListener = {
    WaylandLayerSurface::Impl::outputGeometry, WaylandLayerSurface::Impl::outputMode,
    WaylandLayerSurface::Impl::outputDone,     WaylandLayerSurface::Impl::outputScale,
    WaylandLayerSurface::Impl::outputName,     WaylandLayerSurface::Impl::outputDescription};
const wl_seat_listener kSeatListener = {WaylandLayerSurface::Impl::seatCapabilities,
                                        WaylandLayerSurface::Impl::seatName};
wl_pointer_listener makePointerListener() {
    wl_pointer_listener listener = {};
    listener.enter = WaylandLayerSurface::Impl::pointerEnter;
    listener.leave = WaylandLayerSurface::Impl::pointerLeave;
    listener.motion = WaylandLayerSurface::Impl::pointerMotion;
    listener.button = WaylandLayerSurface::Impl::pointerButton;
    listener.axis = WaylandLayerSurface::Impl::pointerAxis;
    listener.frame = WaylandLayerSurface::Impl::pointerFrame;
    listener.axis_source = WaylandLayerSurface::Impl::pointerAxisSource;
    listener.axis_stop = WaylandLayerSurface::Impl::pointerAxisStop;
    listener.axis_discrete = WaylandLayerSurface::Impl::pointerAxisDiscrete;
    return listener;
}
const wl_pointer_listener kPointerListener = makePointerListener();
wl_keyboard_listener makeKeyboardListener() {
    wl_keyboard_listener listener = {};
    listener.keymap = WaylandLayerSurface::Impl::keyboardKeymap;
    listener.enter = WaylandLayerSurface::Impl::keyboardEnter;
    listener.leave = WaylandLayerSurface::Impl::keyboardLeave;
    listener.key = WaylandLayerSurface::Impl::keyboardKey;
    listener.modifiers = WaylandLayerSurface::Impl::keyboardModifiers;
    listener.repeat_info = WaylandLayerSurface::Impl::keyboardRepeatInfo;
    return listener;
}
const wl_keyboard_listener kKeyboardListener = makeKeyboardListener();
const zwlr_layer_surface_v1_listener kLayerListener = {WaylandLayerSurface::Impl::layerConfigure,
                                                       WaylandLayerSurface::Impl::layerClosed};
}  // namespace

void WaylandLayerSurface::Impl::registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                                               uint32_t version) {
    auto* self = static_cast<Impl*>(data);
    const std::string_view type = interface;
    if (type == wl_compositor_interface.name) {
        self->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (type == zwlr_layer_shell_v1_interface.name) {
        self->layer_shell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (type == wl_seat_interface.name && !self->seat) {
        self->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, kMaxSeatVersion)));
        wl_seat_add_listener(self->seat, &kSeatListener, self);
    } else if (type == wl_output_interface.name) {
        auto output = std::make_unique<Output>();
        output->owner = self;
        output->proxy = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, kMaxOutputVersion)));
        wl_output_add_listener(output->proxy, &kOutputListener, output.get());
        self->outputs.push_back(std::move(output));
    }
}

void WaylandLayerSurface::Impl::outputScale(void* data, wl_output*, int32_t factor) {
    auto* output = static_cast<Output*>(data);
    output->scale = std::max(1, factor);
    if (output == output->owner->target) output->owner->applyScale();
}

void WaylandLayerSurface::Impl::outputName(void* data, wl_output*, const char* name) {
    static_cast<Output*>(data)->name = name;
}

void WaylandLayerSurface::Impl::seatCapabilities(void* data, wl_seat*, uint32_t capabilities) {
    static_cast<Impl*>(data)->updateSeat(capabilities);
}

void WaylandLayerSurface::Impl::pointerEnter(void* data, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t x,
                                             wl_fixed_t y) {
    auto* self = static_cast<Impl*>(data);
    self->onPointerEnterLeave(true);
    self->onPointerMotion(x, y);
}

void WaylandLayerSurface::Impl::pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*) {
    static_cast<Impl*>(data)->onPointerEnterLeave(false);
}

void WaylandLayerSurface::Impl::pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    static_cast<Impl*>(data)->onPointerMotion(x, y);
}

void WaylandLayerSurface::Impl::pointerButton(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button,
                                              uint32_t state) {
    static_cast<Impl*>(data)->onPointerButton(button, state);
}

void WaylandLayerSurface::Impl::keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
#ifdef LWE_HAVE_XKBCOMMON
    auto* self = static_cast<Impl*>(data);
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && fd >= 0 && size > 0) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!self->xkb_ctx) self->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* keymap = xkb_keymap_new_from_string(self->xkb_ctx, (const char*)map, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (keymap) {
                if (self->keymap) xkb_keymap_unref(self->keymap);
                self->keymap = keymap;
                if (self->kb_state) xkb_state_unref(self->kb_state);
                self->kb_state = xkb_state_new(self->keymap);
            }
        }
    }
#else
    (void)data;
    (void)format;
    (void)size;
#endif
    if (fd >= 0) close(fd);
}

void WaylandLayerSurface::Impl::keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                                            uint32_t state) {
    static_cast<Impl*>(data)->onKey(key, state);
}

void WaylandLayerSurface::Impl::keyboardModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed,
                                                  uint32_t latched, uint32_t locked, uint32_t group) {
    static_cast<Impl*>(data)->onModifiers(depressed, latched, locked, group);
}

void WaylandLayerSurface::Impl::layerConfigure(void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w,
                                               uint32_t h) {
    static_cast<Impl*>(data)->onConfigure(serial, w, h);
}

void WaylandLayerSurface::Impl::layerClosed(void* data, zwlr_layer_surface_v1*) {
    static_cast<Impl*>(data)->closed = true;
}

void WaylandLayerSurface::Impl::updateSeat(uint32_t capabilities) {
    const bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (has_pointer && !pointer) {
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &kPointerListener, this);
    } else if (!has_pointer && pointer) {
        wl_pointer_release(pointer);
        pointer = nullptr;
    }
    const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_keyboard && !keyboard) {
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &kKeyboardListener, this);
    } else if (!has_keyboard && keyboard) {
        wl_keyboard_release(keyboard);
        keyboard = nullptr;
    }
}

void WaylandLayerSurface::Impl::onPointerMotion(wl_fixed_t x, wl_fixed_t y) {
    if (!pointer_handler) return;
    pointer_handler(static_cast<float>(wl_fixed_to_double(x)) * scale,
                    static_cast<float>(wl_fixed_to_double(y)) * scale);
}

void WaylandLayerSurface::Impl::onPointerButton(uint32_t button, uint32_t state) {
    if (button_handler) button_handler(button, state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void WaylandLayerSurface::Impl::onPointerEnterLeave(bool entered) {
    if (enter_leave_handler) enter_leave_handler(entered);
}

void WaylandLayerSurface::Impl::onKey(uint32_t key, uint32_t state) {
    const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (key_handler) key_handler(key, pressed);
#ifdef LWE_HAVE_XKBCOMMON
    if (pressed && char_handler && kb_state) {
        // Wayland key codes are evdev codes; xkb key codes are offset by 8.
        const uint32_t codepoint = xkb_state_key_get_utf32(kb_state, key + 8);
        if (codepoint != 0) char_handler(codepoint);
    }
#endif
}

void WaylandLayerSurface::Impl::onModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
#ifdef LWE_HAVE_XKBCOMMON
    if (kb_state) xkb_state_update_mask(kb_state, depressed, latched, locked, 0, 0, group);
#else
    (void)depressed;
    (void)latched;
    (void)locked;
    (void)group;
#endif
}

void WaylandLayerSurface::Impl::onConfigure(uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    const int new_width = w > 0 ? static_cast<int>(w) : config.width;
    const int new_height = h > 0 ? static_cast<int>(h) : config.height;
    if (new_width != width || new_height != height) resized = true;
    width = new_width;
    height = new_height;
    configured = true;
}

void WaylandLayerSurface::Impl::applyScale() {
    const int new_scale = target ? target->scale : 1;
    if (new_scale == scale) return;
    scale = new_scale;
    resized = true;
    if (surface) wl_surface_set_buffer_scale(surface, scale);
}

bool WaylandLayerSurface::Impl::connect() {
    display = wl_display_connect(nullptr);
    if (!display) {
        LOG_E("[LAYER] cannot connect to the Wayland display");
        return false;
    }
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, this);
    // The second round trip delivers the name and scale events of the bound outputs.
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    if (!compositor || !layer_shell) {
        LOG_E("[LAYER] the compositor does not provide %s", !compositor ? "wl_compositor" : "zwlr_layer_shell_v1");
        return false;
    }
    return true;
}

bool WaylandLayerSurface::Impl::chooseOutput() {
    std::vector<std::string> names;
    for (const auto& output : outputs) names.push_back(output->name);
    if (config.output.empty()) return true;
    const int index = layer_options::findOutput(names, config.output);
    if (index < 0) {
        std::string available;
        for (const std::string& name : names) available += (available.empty() ? "" : ", ") + name;
        LOG_E("[LAYER] output '%s' not found (available: %s)", config.output.c_str(), available.c_str());
        return false;
    }
    target = outputs[static_cast<size_t>(index)].get();
    return true;
}

bool WaylandLayerSurface::Impl::createSurface() {
    surface = wl_compositor_create_surface(compositor);
    layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, target ? target->proxy : nullptr,
                                                          static_cast<uint32_t>(config.layer), kNamespace);
    zwlr_layer_surface_v1_add_listener(layer_surface, &kLayerListener, this);
    zwlr_layer_surface_v1_set_size(layer_surface, static_cast<uint32_t>(config.width),
                                   static_cast<uint32_t>(config.height));
    zwlr_layer_surface_v1_set_anchor(layer_surface, config.anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, -1);
#if DEBUG_BUILD
    // Let a debug layer take keyboard focus (click it) so the inspector can be toggled with F8.
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface,
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
#else
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
#endif
    applyScale();
    wl_surface_commit(surface);
    while (!configured && !closed && wl_display_roundtrip(display) >= 0) {
    }
    if (!configured || width <= 0 || height <= 0) {
        LOG_E("[LAYER] the compositor did not configure the layer surface");
        return false;
    }
    return true;
}

WaylandLayerSurface::Impl::~Impl() {
    if (pointer) wl_pointer_release(pointer);
    if (keyboard) wl_keyboard_release(keyboard);
#ifdef LWE_HAVE_XKBCOMMON
    if (kb_state) xkb_state_unref(kb_state);
    if (keymap) xkb_keymap_unref(keymap);
    if (xkb_ctx) xkb_context_unref(xkb_ctx);
#endif
    if (seat) wl_seat_destroy(seat);
    if (layer_surface) zwlr_layer_surface_v1_destroy(layer_surface);
    if (surface) wl_surface_destroy(surface);
    for (const auto& output : outputs) wl_output_destroy(output->proxy);
    if (layer_shell) zwlr_layer_shell_v1_destroy(layer_shell);
    if (compositor) wl_compositor_destroy(compositor);
    if (registry) wl_registry_destroy(registry);
    if (display) {
        wl_display_flush(display);
        wl_display_disconnect(display);
    }
}

WaylandLayerSurface::WaylandLayerSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

WaylandLayerSurface::~WaylandLayerSurface() = default;

std::unique_ptr<WaylandLayerSurface> WaylandLayerSurface::create(const LayerSurfaceConfig& config) {
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    if (!impl->connect() || !impl->chooseOutput() || !impl->createSurface()) return nullptr;
    LOG_I("[LAYER] surface on output '%s': %dx%d logical, scale %d",
          impl->target ? impl->target->name.c_str() : "(compositor choice)", impl->width, impl->height, impl->scale);
    return std::unique_ptr<WaylandLayerSurface>(new WaylandLayerSurface(std::move(impl)));
}

wl_display* WaylandLayerSurface::display() const {
    return impl_->display;
}

wl_surface* WaylandLayerSurface::surface() const {
    return impl_->surface;
}

int WaylandLayerSurface::pixelWidth() const {
    return impl_->width * impl_->scale;
}

int WaylandLayerSurface::pixelHeight() const {
    return impl_->height * impl_->scale;
}

int WaylandLayerSurface::scale() const {
    return impl_->scale;
}

bool WaylandLayerSurface::closed() const {
    return impl_->closed;
}

bool WaylandLayerSurface::takeResize() {
    const bool resized = impl_->resized;
    impl_->resized = false;
    return resized;
}

void WaylandLayerSurface::setPointerHandler(PointerHandler handler) {
    impl_->pointer_handler = std::move(handler);
}

void WaylandLayerSurface::setButtonHandler(ButtonHandler handler) {
    impl_->button_handler = std::move(handler);
}

void WaylandLayerSurface::setEnterLeaveHandler(EnterLeaveHandler handler) {
    impl_->enter_leave_handler = std::move(handler);
}

void WaylandLayerSurface::setKeyHandler(KeyHandler handler) {
    impl_->key_handler = std::move(handler);
}

void WaylandLayerSurface::setCharHandler(CharHandler handler) {
    impl_->char_handler = std::move(handler);
}

bool WaylandLayerSurface::pump() {
    wl_display* display = impl_->display;
    while (wl_display_prepare_read(display) != 0) wl_display_dispatch_pending(display);
    wl_display_flush(display);
    pollfd fd = {wl_display_get_fd(display), POLLIN, 0};
    if (poll(&fd, 1, 0) > 0)
        wl_display_read_events(display);
    else
        wl_display_cancel_read(display);
    wl_display_dispatch_pending(display);
    return wl_display_get_error(display) == 0;
}
