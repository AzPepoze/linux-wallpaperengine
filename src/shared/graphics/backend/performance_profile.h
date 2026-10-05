#pragma once

namespace performance_profile {
void initialize(bool enabled);
bool enabled();
void beginFrame(double presented_seconds);
void beginRender();
void endRender();
void recordAcquire(double milliseconds);
void recordPresent(double milliseconds);
void recordCpuFrame(double milliseconds);
void shutdown();
}  // namespace performance_profile
