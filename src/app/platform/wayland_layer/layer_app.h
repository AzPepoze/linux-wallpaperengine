#ifndef LAYER_APP_H
#define LAYER_APP_H

#include <memory>

#include "app/cli_options.h"
#include "app/platform/layer_backend.h"

// The Wayland layer-shell backend, built into the wayland plugin.
class LayerApp : public LayerBackend {
   public:
    // Null, with the reason logged, when a layer surface cannot be set up in this session.
    static std::unique_ptr<LayerApp> create(const CliOptions& cli);
    ~LayerApp() override;

    int run(const LayerAppCallbacks& callbacks) override;

   private:
    struct Impl;
    explicit LayerApp(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

#endif  // LAYER_APP_H
