#include "app/platform/wayland_layer/layer_app.h"

#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <atomic>

#include "app/platform/layer_options.h"
#include "app/platform/wayland_layer/wayland_layer_surface.h"
#include "app/platform/wayland_layer/wayland_vulkan_swapchain.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"

namespace {
constexpr double kMaxFrameSeconds = 0.1;

double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
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
};

void LayerApp::Impl::trackFrameTime() {
    const double now = nowSeconds();
    if (last_frame > 0.0) frame_seconds = std::min(now - last_frame, kMaxFrameSeconds);
    last_frame = now;
}

void LayerApp::Impl::forwardPointer(float x, float y, void (*event)(const sapp_event*)) {
    sapp_event e = {};
    e.type = SAPP_EVENTTYPE_MOUSE_MOVE;
    e.mouse_x = x;
    e.mouse_y = y;
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
                                                     static_cast<uint32_t>(impl->wayland->pixelHeight()));
    if (!impl->swapchain) return nullptr;
    return std::unique_ptr<LayerApp>(new LayerApp(std::move(impl)));
}

int LayerApp::run(const LayerAppCallbacks& callbacks) {
    Impl& app = *impl_;
    surface::setProvider(&app);
    app.wayland->setPointerHandler([&](float x, float y) { app.forwardPointer(x, y, callbacks.event); });

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
