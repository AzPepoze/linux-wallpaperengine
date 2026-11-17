#ifndef HEADLESS_SURFACE_H
#define HEADLESS_SURFACE_H

#include <atomic>

#include "shared/graphics/backend/surface.h"
#include "sokol_gfx.h"

namespace headless {

// Sets the offscreen frame size and rate. Call before run().
void setSize(int width, int height, int fps);

// Surface without a window: frames render into an offscreen color target.
class Surface : public SurfaceProvider {
   public:
    explicit Surface(sg_environment environment);
    ~Surface() override;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    int width() const override;
    int height() const override;
    float dpiScale() const override;
    double frameDuration() const override;
    void requestQuit() override;
    sg_environment environment() override;
    sg_swapchain acquireSwapchain() override;
    bool hasSwapchain() const override;
    sg_attachments offscreenAttachments() override;
    sg_image offscreenColor() override;

    bool quitRequested() const;
    // Destroys the offscreen images and views. Call before sg_shutdown.
    void releaseTargets();

   private:
    void ensureTargets();

    sg_environment environment_ = {};
    sg_image color_ = {SG_INVALID_ID};
    sg_view color_view_ = {SG_INVALID_ID};
    sg_image depth_ = {SG_INVALID_ID};
    sg_view depth_view_ = {SG_INVALID_ID};
    sg_attachments attachments_ = {};
    std::atomic<bool> quit_{false};
};

}  // namespace headless

#endif  // HEADLESS_SURFACE_H
