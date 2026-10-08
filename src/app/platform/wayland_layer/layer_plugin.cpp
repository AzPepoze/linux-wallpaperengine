// Entry points of the wayland plugin, resolved by the host when -r is used on a Wayland session.
#include "app/platform/wayland_layer/layer_app.h"
#include "shared/core/plugin.h"

extern "C" {
int lwe_plugin_abi() {
    return kPluginAbi;
}

LayerBackend* lwe_create_layer_backend(const CliOptions& cli) {
    return LayerApp::create(cli).release();
}
}
