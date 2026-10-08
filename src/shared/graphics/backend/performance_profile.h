#pragma once

#include "shared/core/host_api.h"

namespace performance_profile {
void initialize(bool enabled);
LWE_HOST_API bool enabled();
void beginFrame(double presented_seconds);
void beginRender();
void endRender();
void recordAcquire(double milliseconds);
LWE_HOST_API void recordPresent(double milliseconds);
void recordCpuFrame(double milliseconds);
void shutdown();
}  // namespace performance_profile
