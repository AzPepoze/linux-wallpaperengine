#pragma once

#include <string>
#include <vector>

#include "shared/core/host_api.h"

namespace performance_profile {
struct GpuSpanStat {
    std::string label;
    double mean_ms;
    size_t samples;
};

void initialize(bool enabled);
LWE_HOST_API bool enabled();
// Per-pass GPU means from the last 10 s window, slowest first.
const std::vector<GpuSpanStat>& gpuSpanStats();
void beginFrame(double presented_seconds);
void beginRender();
void endRender();
void recordAcquire(double milliseconds);
LWE_HOST_API void recordPresent(double milliseconds);
void recordCpuFrame(double milliseconds);
void shutdown();
}  // namespace performance_profile
