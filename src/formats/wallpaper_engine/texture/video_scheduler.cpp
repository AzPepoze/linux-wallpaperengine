#include "video_scheduler.h"

#include <algorithm>
#include <cmath>
#include <thread>

VideoScheduler::VideoScheduler() = default;

void VideoScheduler::reset() {
    has_start_ = false;
    start_pts_sec_ = 0.0;
    last_pts_sec_ = 0.0;
}

void VideoScheduler::set_time_base_and_fps(double time_base, double fps) {
    time_base_ = time_base;
    fps_ = fps;
    frame_duration_sec_ = fps_ > 0.0 ? 1.0 / fps_ : (1.0 / 60.0);
    reset();
}

double VideoScheduler::compute_target_time(int64_t pts) {
    return static_cast<double>(pts) * time_base_;
}

double VideoScheduler::get_playback_elapsed_sec() const {
    if (!has_start_) return 0.0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_clock_).count();
}

bool VideoScheduler::should_present_frame(int64_t pts, PlaybackStats& stats, PerformanceTiming& perf,
                                          bool& out_is_late) {
    auto t0 = std::chrono::steady_clock::now();
    double pts_sec = compute_target_time(pts);

    if (!has_start_) {
        start_clock_ = t0;
        start_pts_sec_ = pts_sec;
        last_pts_sec_ = pts_sec;
        has_start_ = true;
    }

    double elapsed_wall_sec = std::chrono::duration<double>(t0 - start_clock_).count();
    double target_playback_sec = pts_sec - start_pts_sec_;
    double diff_ms = (elapsed_wall_sec - target_playback_sec) * 1000.0;

    stats.current_pts_sec = pts_sec;
    stats.playback_clock_sec = elapsed_wall_sec;
    stats.av_timing_error_ms = diff_ms;

    out_is_late = (diff_ms > (frame_duration_sec_ * 1000.0 * 0.5));
    if (out_is_late) {
        ++stats.late_frames;
        double abs_diff = std::abs(diff_ms);
        stats.sum_lateness_ms += abs_diff;
        if (abs_diff > stats.max_lateness_ms) stats.max_lateness_ms = abs_diff;
    }

    auto t1 = std::chrono::steady_clock::now();
    perf.scheduler_cpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    last_pts_sec_ = pts_sec;
    return true;
}

bool VideoScheduler::should_drop_stale_frame(int64_t pts, PlaybackStats& stats) {
    if (!has_start_) return false;
    double pts_sec = compute_target_time(pts);
    auto now = std::chrono::steady_clock::now();
    double elapsed_wall_sec = std::chrono::duration<double>(now - start_clock_).count();
    double target_playback_sec = pts_sec - start_pts_sec_;
    double diff_ms = (elapsed_wall_sec - target_playback_sec) * 1000.0;

    if (diff_ms > (frame_duration_sec_ * 1000.0 * 2.0)) {
        ++stats.dropped_frames;
        return true;
    }
    return false;
}

void VideoScheduler::wait_until_target(int64_t pts) {
    if (!has_start_) return;
    double pts_sec = compute_target_time(pts);
    auto target_time = start_clock_ + std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::duration<double>(pts_sec - start_pts_sec_));
    auto now = std::chrono::steady_clock::now();
    if (target_time > now) {
        auto wait_duration = target_time - now;
        if (wait_duration > std::chrono::milliseconds(1)) {
            std::this_thread::sleep_for(wait_duration - std::chrono::microseconds(500));
        }
        while (std::chrono::steady_clock::now() < target_time) {
            // Spin-wait final few microseconds for precision
        }
    }
}
