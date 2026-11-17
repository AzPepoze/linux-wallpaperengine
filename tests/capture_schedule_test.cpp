#include "capture/capture_options.h"
#include "capture/capture_schedule.h"

#include "test_util.h"

int main() {
    // Frame ticks at 60 fps, passed as i / 60.0 so the times are exact at whole and half seconds.
    auto frameTime = [](int frame) { return frame / 60.0; };

    capture::CaptureOptions times;
    times.enabled = true;
    times.at_times = {0.2, 1, 3};
    capture::CaptureSchedule timed(times);
    for (int frame = 0; frame <= 210; ++frame) {
        timed.shotDue(frameTime(frame), true);
    }
    CHECK(timed.shotsTaken() == 3);
    CHECK(timed.finished());

    capture::CaptureOptions stream;
    stream.enabled = true;
    stream.every_seconds = 0.5;
    stream.from = 0;
    stream.until = 2;
    capture::CaptureSchedule every(stream);
    for (int frame = 0; frame <= 180; ++frame) {
        every.shotDue(frameTime(frame), true);
    }
    CHECK(every.shotsTaken() == 5);
    CHECK(every.finished());

    capture::CaptureOptions limited;
    limited.enabled = true;
    limited.every_frame = true;
    limited.limit = 4;
    capture::CaptureSchedule frames(limited);
    int frame_shots = 0;
    for (int frame = 0; frame < 100; ++frame) {
        if (frames.shotDue(frameTime(frame), true)) ++frame_shots;
    }
    CHECK(frame_shots == 4);
    CHECK(frames.shotsTaken() == 4);
    CHECK(frames.finished());

    capture::CaptureOptions no_frame_tick;
    no_frame_tick.enabled = true;
    no_frame_tick.every_frame = true;
    capture::CaptureSchedule idle(no_frame_tick);
    CHECK(!idle.shotDue(0.0, false));
    CHECK(idle.shotDue(0.0, true));
    CHECK(!idle.finished());

    capture::CaptureOptions one_time;
    one_time.enabled = true;
    one_time.at_times = {1, 1.5};
    capture::CaptureSchedule jumped(one_time);
    CHECK(jumped.shotDue(2.0, false));
    CHECK(!jumped.finished());
    CHECK(jumped.shotDue(2.0, false));
    CHECK(!jumped.shotDue(2.0, false));
    CHECK(jumped.finished());

    capture::CaptureOptions empty;
    capture::CaptureSchedule nothing(empty);
    CHECK(nothing.finished());
    CHECK(!nothing.shotDue(5.0, true));

    return test::finish("capture schedule");
}
