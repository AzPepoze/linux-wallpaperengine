#ifndef FRAME_RATE_H
#define FRAME_RATE_H

namespace frame_rate {

// Software cap + vsync choice for --fps; <=0 means unset (vsync, no cap).
struct Policy {
    int software_limit;
    bool vsync;
};

Policy policyFor(int fps_limit);

class Meter {
   public:
    void tick(double dt_seconds);
    double fps() const {
        return fps_;
    }

   private:
    double accum_ = 0.0;
    int frames_ = 0;
    double fps_ = 0.0;
};

}  // namespace frame_rate

#endif  // FRAME_RATE_H
