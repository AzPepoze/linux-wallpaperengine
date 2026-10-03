#ifndef LAYER_APP_H
#define LAYER_APP_H

#include <memory>

#include "app/cli_options.h"
#include "sokol_app.h"

struct LayerAppCallbacks {
    void (*init)();
    void (*frame)();
    void (*event)(const sapp_event*);
    void (*cleanup)();
};

class LayerApp {
   public:
    // Null, with the reason logged, when a layer surface cannot be set up in this session.
    static std::unique_ptr<LayerApp> create(const CliOptions& cli);
    ~LayerApp();

    // Runs until the compositor closes the surface or a quit is requested. Returns the exit code.
    int run(const LayerAppCallbacks& callbacks);

   private:
    struct Impl;
    explicit LayerApp(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

#endif  // LAYER_APP_H
