#include "performance_profile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>

#include "gpu_timing.h"
#include "shared/core/logger.h"

namespace performance_profile {
namespace {
struct Aggregate {
    double sum = 0.0;
    size_t count = 0;
    void add(double value) {
        sum += value;
        ++count;
    }
    double mean() const {
        return count ? sum / count : 0.0;
    }
};
struct State {
    bool enabled = false;
    bool gpu = false;
    double warmup = 0.0;
    double seconds = 0.0;
    double frame_acquire_ms = 0.0;
    double frame_resource_ms = 0.0;
    std::vector<double> intervals;
    Aggregate cpu, acquire, resources, present;
    std::map<std::string, Aggregate> gpu_spans;
    std::vector<GpuSpanStat> latest_gpu;
} state;
}  // namespace

void initialize(bool enabled) {
    state = {};
    state.enabled = enabled;
    if (!enabled) return;
    state.gpu = gpu_timing_initialize();
    LOG_I("Performance profiling: GPU timestamps %s (render spans include GPU queue/presentation dependencies)",
          state.gpu ? "enabled" : "unavailable; CPU and frame intervals only");
}

bool enabled() {
    return state.enabled;
}

const std::vector<GpuSpanStat>& gpuSpanStats() {
    return state.latest_gpu;
}

void beginFrame(double dt) {
    if (!state.enabled) return;
    const auto samples = state.gpu ? gpu_timing_poll() : std::vector<GpuTimingSample>{};
    state.frame_acquire_ms = state.frame_resource_ms = 0.0;
    if (!(dt > 0.0) || !std::isfinite(dt)) return;
    if (state.warmup < 5.0) {
        state.warmup += dt;
        return;
    }
    for (const auto& sample : samples) state.gpu_spans[sample.label].add(sample.milliseconds);
    state.seconds += dt;
    state.intervals.push_back(dt * 1000.0);
    if (state.seconds < 10.0 && state.intervals.size() < 100000) return;
    const double average =
        std::accumulate(state.intervals.begin(), state.intervals.end(), 0.0) / state.intervals.size();
    std::sort(state.intervals.begin(), state.intervals.end());
    const size_t p95 = (size_t)std::ceil(state.intervals.size() * 0.95) - 1;
    LOG_I("Performance: %.1f presented FPS, mean %.2f ms, p95 %.2f ms (%zu frames)",
          state.intervals.size() / state.seconds, average, state.intervals[p95], state.intervals.size());
    LOG_I("Performance CPU: work %.3f ms, swapchain acquire %.3f ms", state.cpu.mean(), state.acquire.mean());
    if (state.resources.count) LOG_I("Performance CPU: frame resource acquisition %.3f ms", state.resources.mean());
    if (state.present.count)
        LOG_I("Performance CPU: present %.3f ms", state.present.mean());
    else
        LOG_I("Performance CPU: present unavailable on this window backend");
    state.latest_gpu.clear();
    for (const auto& [label, span] : state.gpu_spans) state.latest_gpu.push_back({label, span.mean(), span.count});
    std::sort(state.latest_gpu.begin(), state.latest_gpu.end(),
              [](const GpuSpanStat& a, const GpuSpanStat& b) { return a.mean_ms > b.mean_ms; });
    for (const GpuSpanStat& span : state.latest_gpu)
        LOG_I("Performance GPU: %s mean %.3f ms (%zu samples)", span.label.c_str(), span.mean_ms, span.samples);
    state.seconds = 0.0;
    state.intervals.clear();
    state.cpu = state.acquire = state.resources = state.present = {};
    state.gpu_spans.clear();
}

void beginRender() {
    if (!state.gpu) return;
    const auto start = std::chrono::steady_clock::now();
    gpu_timing_begin_frame();
    state.frame_resource_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (state.warmup >= 5.0) state.resources.add(state.frame_resource_ms);
}
void endRender() {
    if (state.gpu) gpu_timing_end_frame();
}
void recordAcquire(double ms) {
    if (!state.enabled) return;
    state.frame_acquire_ms += ms;
    if (state.warmup >= 5.0) state.acquire.add(ms);
}
void recordPresent(double ms) {
    if (state.enabled && state.warmup >= 5.0) state.present.add(ms);
}
void recordCpuFrame(double ms) {
    if (state.enabled && state.warmup >= 5.0)
        state.cpu.add(std::max(0.0, ms - state.frame_acquire_ms - state.frame_resource_ms));
}
void shutdown() {
    if (state.gpu) gpu_timing_shutdown();
    state = {};
}
}  // namespace performance_profile
