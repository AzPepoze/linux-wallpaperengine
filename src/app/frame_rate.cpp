#include "app/frame_rate.h"

namespace frame_rate {

Policy policyFor(int fps_limit) {
    if (fps_limit <= 0) return {0, true};
    return {fps_limit, false};
}

void Meter::tick(double dt_seconds) {
    if (dt_seconds <= 0.0) return;
    accum_ += dt_seconds;
    ++frames_;
    if (accum_ >= 0.5) {
        fps_ = frames_ / accum_;
        accum_ = 0.0;
        frames_ = 0;
    }
}

}  // namespace frame_rate
