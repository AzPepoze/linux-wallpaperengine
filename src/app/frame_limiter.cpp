#include "app/frame_limiter.h"

#include <time.h>

namespace {
long long nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
}  // namespace

void limitFrameRate(int fps) {
    static long long next_frame_ns = 0;
    if (fps <= 0) return;
    const long long step = 1000000000LL / fps;
    const long long now = nowNs();
    if (next_frame_ns == 0 || now - next_frame_ns > step * 4) next_frame_ns = now;
    next_frame_ns += step;
    if (next_frame_ns > now) {
        const long long wait = next_frame_ns - now;
        timespec ts{static_cast<time_t>(wait / 1000000000LL), static_cast<long>(wait % 1000000000LL)};
        nanosleep(&ts, nullptr);
    }
}
