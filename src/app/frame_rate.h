#ifndef FRAME_RATE_H
#define FRAME_RATE_H

namespace frame_rate {

// Frame-rate policy derived from the --fps option. An unset (or non-positive)
// limit means "no software cap": rely on vsync at the display refresh rate.
struct Policy {
    int software_limit;  // 0 = no software cap
    bool vsync;
};

Policy policyFor(int fps_limit);

// Measures the real presented frame rate over a short rolling window.
class Meter {
   public:
    // Call once per presented frame with the wall-clock seconds since the last call.
    void tick(double dt_seconds);
    double fps() const { return fps_; }

   private:
    double accum_ = 0.0;
    int frames_ = 0;
    double fps_ = 0.0;
};

}  // namespace frame_rate

#endif  // FRAME_RATE_H
