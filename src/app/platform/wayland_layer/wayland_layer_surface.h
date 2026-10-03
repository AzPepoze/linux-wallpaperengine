#ifndef WAYLAND_LAYER_SURFACE_H
#define WAYLAND_LAYER_SURFACE_H

#include <stdint.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/platform/layer_options.h"

struct wl_display;
struct wl_surface;

struct LayerSurfaceConfig {
    std::string output;
    layer_options::Layer layer = layer_options::Layer::Background;
    uint32_t anchor = layer_options::kAnchorAll;
    int width = 0;
    int height = 0;
};

// A wlr-layer-shell surface bound to one wl_output, plus the pointer events delivered to it.
class WaylandLayerSurface {
   public:
    struct Impl;
    using PointerHandler = std::function<void(float x, float y)>;

    static std::unique_ptr<WaylandLayerSurface> create(const LayerSurfaceConfig& config);
    ~WaylandLayerSurface();
    WaylandLayerSurface(const WaylandLayerSurface&) = delete;
    WaylandLayerSurface& operator=(const WaylandLayerSurface&) = delete;

    wl_display* display() const;
    wl_surface* surface() const;
    int pixelWidth() const;
    int pixelHeight() const;
    int scale() const;
    bool closed() const;
    bool takeResize();
    void setPointerHandler(PointerHandler handler);

    // Reads and dispatches pending events without blocking. False once the connection is broken.
    bool pump();

   private:
    explicit WaylandLayerSurface(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

#endif  // WAYLAND_LAYER_SURFACE_H
