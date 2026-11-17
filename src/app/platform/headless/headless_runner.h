#ifndef HEADLESS_RUNNER_H
#define HEADLESS_RUNNER_H

#include "sokol_app.h"

namespace headless {

// Runs the engine loop without a window until quit or the capture finishes. Returns the exit code.
int run(const sapp_desc& desc);

}  // namespace headless

#endif  // HEADLESS_RUNNER_H
