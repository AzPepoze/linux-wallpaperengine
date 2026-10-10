// Wayland plugin entry points resolved by the host when -r is used in a Wayland session.
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/platform/wayland_layer/layer_app.h"
#include "shared/core/plugin.h"

namespace {
void outputName(void* data, wl_output*, const char* name) {
    static_cast<std::vector<std::string>*>(data)->push_back(name);
}

// libwayland aborts on a null event handler, so the events this listing ignores get empty ones.
void ignoreOutputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                          int32_t) {}
void ignoreOutputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void ignoreOutputDone(void*, wl_output*) {}
void ignoreOutputScale(void*, wl_output*, int32_t) {}
void ignoreOutputDescription(void*, wl_output*, const char*) {}

wl_output_listener makeOutputListener() {
    wl_output_listener listener = {};
    listener.geometry = ignoreOutputGeometry;
    listener.mode = ignoreOutputMode;
    listener.done = ignoreOutputDone;
    listener.scale = ignoreOutputScale;
    listener.name = outputName;
    listener.description = ignoreOutputDescription;
    return listener;
}
const wl_output_listener kOutputListener = makeOutputListener();

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) != 0) return;
    // Output names arrive with version 4 of wl_output.
    wl_output* output =
        static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
    wl_output_add_listener(output, &kOutputListener, data);
}

void registryRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {registryGlobal, registryRemove};

// Prints the output names of the Wayland session, one per line.
bool printOutputNames() {
    wl_display* display = wl_display_connect(nullptr);
    if (!display) return false;

    std::vector<std::string> names;
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, &names);
    // The first round trip binds the outputs; the second delivers their names.
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    for (const std::string& name : names) printf("%s\n", name.c_str());

    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return true;
}
}  // namespace

extern "C" {
int lwe_plugin_abi() {
    return kPluginAbi;
}

LayerBackend* lwe_create_layer_backend(const CliOptions& cli) {
    return LayerApp::create(cli).release();
}

bool lwe_list_outputs() {
    return printOutputNames();
}
}
