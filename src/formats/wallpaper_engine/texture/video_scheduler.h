#pragma once

#include <chrono>
#include <cstdint>

#include "video_types.h"

class VideoScheduler {
   public:
    VideoScheduler();

    void reset();
    void set_time_base_and_fps(double time_base, double fps);

    double compute_target_time(int64_t pts);
    bool should_present_frame(int64_t pts, PlaybackStats& stats, PerformanceTiming& perf, bool& out_is_late);
    bool should_drop_stale_frame(int64_t pts, PlaybackStats& stats);
    void wait_until_target(int64_t pts);

    double get_playback_elapsed_sec() const;

   private:
    double time_base_ = 0.0;
    double fps_ = 60.0;
    double frame_duration_sec_ = 1.0 / 60.0;

    bool has_start_ = false;
    std::chrono::steady_clock::time_point start_clock_;
    double start_pts_sec_ = 0.0;
    double last_pts_sec_ = 0.0;
};
