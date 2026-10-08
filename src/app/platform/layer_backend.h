#ifndef LAYER_BACKEND_H
#define LAYER_BACKEND_H

#include "app/cli_options.h"
#include "sokol_app.h"

struct LayerAppCallbacks {
    void (*init)();
    void (*frame)();
    void (*event)(const sapp_event*);
    void (*cleanup)();
};

// Runs the wallpaper on a desktop layer surface. Implemented by the wayland plugin.
class LayerBackend {
   public:
    virtual ~LayerBackend() = default;
    // Runs until the compositor closes the surface or a quit is requested. Returns the exit code.
    virtual int run(const LayerAppCallbacks& callbacks) = 0;
};

// Plugin entry point: a backend for `cli`, or null with the reason logged when no layer surface is available.
using CreateLayerBackendFunction = LayerBackend* (*)(const CliOptions& cli);

#endif  // LAYER_BACKEND_H
