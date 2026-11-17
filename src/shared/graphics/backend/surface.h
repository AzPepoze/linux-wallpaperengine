#ifndef SURFACE_H
#define SURFACE_H

#include "shared/core/host_api.h"
#include "sokol_gfx.h"

// Presentation surface for the renderer; platform backends can install a provider.
class SurfaceProvider {
   public:
    virtual ~SurfaceProvider() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual float dpiScale() const = 0;
    virtual double frameDuration() const = 0;
    virtual void requestQuit() = 0;
    virtual sg_environment environment() = 0;
    virtual sg_swapchain acquireSwapchain() = 0;
    // Headless providers render into an offscreen target instead of a swapchain.
    virtual bool hasSwapchain() const { return true; }
    virtual sg_attachments offscreenAttachments() { return {}; }
    virtual sg_image offscreenColor() { return {SG_INVALID_ID}; }
};

namespace surface {
LWE_HOST_API void setProvider(SurfaceProvider* provider);
bool hasProvider();
int width();
int height();
float dpiScale();
double frameDuration();
void requestQuit();
sg_environment environment();
sg_swapchain acquireSwapchain();
bool hasSwapchain();
sg_attachments offscreenAttachments();
sg_image offscreenColor();
}  // namespace surface

#endif  // SURFACE_H
