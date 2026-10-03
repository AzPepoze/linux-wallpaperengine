#include "shared/graphics/backend/surface.h"

#include "sokol_app.h"
#include "sokol_glue.h"

namespace {
SurfaceProvider* g_provider = nullptr;
}

namespace surface {
void setProvider(SurfaceProvider* provider) {
    g_provider = provider;
}

bool hasProvider() {
    return g_provider != nullptr;
}

int width() {
    return g_provider ? g_provider->width() : sapp_width();
}

int height() {
    return g_provider ? g_provider->height() : sapp_height();
}

float dpiScale() {
    return g_provider ? g_provider->dpiScale() : sapp_dpi_scale();
}

double frameDuration() {
    return g_provider ? g_provider->frameDuration() : sapp_frame_duration();
}

void requestQuit() {
    if (g_provider)
        g_provider->requestQuit();
    else
        sapp_request_quit();
}

sg_environment environment() {
    return g_provider ? g_provider->environment() : sglue_environment();
}

sg_swapchain acquireSwapchain() {
    return g_provider ? g_provider->acquireSwapchain() : sglue_swapchain();
}
}  // namespace surface
