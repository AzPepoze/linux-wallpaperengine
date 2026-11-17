#include "app/platform/headless/headless_runner.h"

#include <cstdlib>
#include <memory>

#include "app/platform/headless/headless_device.h"
#include "app/platform/headless/headless_surface.h"
#include "capture/capture_session.h"
#include "shared/graphics/backend/surface.h"

namespace headless {

int run(const sapp_desc& desc) {
    // Fixed seed, so particles and other random effects repeat from run to run.
    srand(1);
    std::unique_ptr<Device> device = Device::create();
    if (!device) return EXIT_FAILURE;

    // The provider must exist before init_cb, because sg_setup reads its environment.
    Surface provider(device->environment());
    surface::setProvider(&provider);

    desc.init_cb();
    while (!provider.quitRequested() && !capture::captureFinished()) desc.frame_cb();

    provider.releaseTargets();
    desc.cleanup_cb();
    surface::setProvider(nullptr);
    device->destroy();
    return EXIT_SUCCESS;
}

}  // namespace headless
