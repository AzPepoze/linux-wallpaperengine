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
}  // namespace surface

#endif  // SURFACE_H
