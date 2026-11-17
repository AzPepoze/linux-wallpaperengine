#include "capture/capture_schedule.h"

namespace capture {

CaptureSchedule::CaptureSchedule(const CaptureOptions& options) : options_(options) {}

bool CaptureSchedule::shotDue(double scene_time, bool frame_tick) {
    if (finished()) return false;
    const bool time_due = atTimeDue(scene_time);
    const bool every_due = everyDue(scene_time);
    const bool frame_due = options_.every_frame && frame_tick;
    if (!time_due && !every_due && !frame_due) return false;
    requested_time_ = time_due ? options_.at_times[next_time_index_] : nextEveryTime();
    if (time_due) ++next_time_index_;
    if (every_due) ++every_index_;
    ++shots_taken_;
    return true;
}

bool CaptureSchedule::finished() const {
    if (shots_taken_ >= options_.limit) return true;
    const bool times_left = next_time_index_ < options_.at_times.size();
    return !times_left && !everyStreamHasMore() && !options_.every_frame;
}

int CaptureSchedule::shotsTaken() const {
    return shots_taken_;
}

double CaptureSchedule::requestedTime() const {
    return requested_time_;
}

bool CaptureSchedule::atTimeDue(double scene_time) const {
    if (next_time_index_ >= options_.at_times.size()) return false;
    return scene_time >= options_.at_times[next_time_index_];
}

bool CaptureSchedule::everyDue(double scene_time) const {
    if (!everyStreamHasMore()) return false;
    return scene_time >= nextEveryTime();
}

bool CaptureSchedule::everyStreamHasMore() const {
    if (options_.every_seconds <= 0) return false;
    if (options_.until < 0) return true;
    return nextEveryTime() <= options_.until;
}

double CaptureSchedule::nextEveryTime() const {
    return options_.from + every_index_ * options_.every_seconds;
}

}  // namespace capture
