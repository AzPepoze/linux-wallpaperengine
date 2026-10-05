#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GpuTimingSample {
    std::string label;
    double milliseconds;
};

inline double gpu_timestamp_ms(uint64_t begin, uint64_t end, unsigned valid_bits, double period_ns) {
    if (valid_bits == 0 || valid_bits > 64 || !(period_ns > 0.0)) return 0.0;
    const uint64_t mask = valid_bits == 64 ? UINT64_MAX : (UINT64_C(1) << valid_bits) - 1;
    return ((end - begin) & mask) * period_ns / 1000000.0;
}

bool gpu_timing_initialize();
// Must run outside a render pass. Polling never waits for query results.
std::vector<GpuTimingSample> gpu_timing_poll();
void gpu_timing_begin_frame();
int gpu_timing_begin_pass(const std::string& label);
void gpu_timing_end_pass(int token);
void gpu_timing_end_frame();
// Caller must finish outstanding GPU work before shutdown.
void gpu_timing_shutdown();
