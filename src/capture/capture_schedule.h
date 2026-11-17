#ifndef CAPTURE_SCHEDULE_H
#define CAPTURE_SCHEDULE_H

#include <cstddef>

#include "capture/capture_options.h"

namespace capture {

// Decides on each frame whether a screenshot is due. Pure: no GPU or I/O.
class CaptureSchedule {
public:
    explicit CaptureSchedule(const CaptureOptions& options);

    // Call once per frame with the current scene time. Returns true at most once per call.
    bool shotDue(double scene_time, bool frame_tick);
    bool finished() const;
    int shotsTaken() const;
    // Scene time the last shot was asked for (the at-time or the repeat time), not the frame time.
    double requestedTime() const;

private:
    bool atTimeDue(double scene_time) const;
    bool everyDue(double scene_time) const;
    bool everyStreamHasMore() const;
    double nextEveryTime() const;

    CaptureOptions options_;
    size_t next_time_index_ = 0;
    int every_index_ = 0;
    int shots_taken_ = 0;
    double requested_time_ = 0.0;
};

}  // namespace capture

#endif  // CAPTURE_SCHEDULE_H
