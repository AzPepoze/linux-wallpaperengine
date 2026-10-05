#include "shared/graphics/backend/performance_profile.h"

#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_timing.h"
#include "test_util.h"

namespace {
bool supported = false;
int initializes = 0, begins = 0, ends = 0, polls = 0, shutdowns = 0;
bool logged(const char* text) {
    for (const auto& entry : logger_recent_entries())
        if (entry.message.find(text) != std::string::npos) return true;
    return false;
}
}  // namespace

// Exercise the reporting/lifecycle against an unsupported device and delayed results.
bool gpu_timing_initialize() {
    ++initializes;
    return supported;
}
std::vector<GpuTimingSample> gpu_timing_poll() {
    ++polls;
    if (polls < 3) return {};
    return {{"full-render", 2.0}};
}
void gpu_timing_begin_frame() {
    ++begins;
}
void gpu_timing_end_frame() {
    ++ends;
}
void gpu_timing_shutdown() {
    ++shutdowns;
}

int main() {
    namespace profile = performance_profile;
    profile::initialize(false);
    profile::beginFrame(1);
    profile::beginRender();
    profile::endRender();
    CHECK(!profile::enabled());
    CHECK(initializes == 0 && begins == 0 && polls == 0);
    profile::shutdown();
    CHECK(shutdowns == 0);

    profile::initialize(true);
    CHECK(profile::enabled());
    CHECK(logged("GPU timestamps unavailable"));
    profile::beginFrame(5);
    profile::recordAcquire(3);
    profile::recordCpuFrame(5);
    profile::recordPresent(4);
    profile::beginRender();
    profile::endRender();
    profile::beginFrame(10);
    CHECK(logged("work 2.000 ms, swapchain acquire 3.000 ms"));
    CHECK(logged("present 4.000 ms"));
    CHECK(begins == 0 && ends == 0 && polls == 0);
    profile::shutdown();
    CHECK(shutdowns == 0);

    logger_clear_recent_entries();
    supported = true;
    profile::initialize(true);
    profile::beginFrame(5);
    profile::beginRender();
    profile::endRender();
    profile::beginFrame(1);  // No completed GPU query yet: reporting still proceeds.
    CHECK(polls == 2);
    profile::beginFrame(9);
    CHECK(logged("full-render mean 2.000 ms"));
    CHECK(logged("present unavailable"));
    CHECK(begins == 1 && ends == 1);
    profile::shutdown();
    CHECK(shutdowns == 1 && !profile::enabled());
    return test::finish("performance profile checks");
}
