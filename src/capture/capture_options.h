#ifndef CAPTURE_OPTIONS_H
#define CAPTURE_OPTIONS_H

#include <string>
#include <vector>

namespace capture {

// Raw flag values as typed; "" means the flag was not given.
struct CaptureFlags {
    std::string capture;
    std::string every;
    std::string from;
    std::string until;
    std::string limit;
    std::string dir;
    std::string size;
    std::string fps;
    std::string screenshot;        // --screenshot <path>: one shot at the exact path
    std::string screenshot_delay;  // --screenshot-delay <s>
    bool dump = false;
    bool window = false;
};

struct CaptureOptions {
    bool enabled = false;
    std::vector<double> at_times;
    double every_seconds = 0;
    bool every_frame = false;
    double from = 0;
    double until = -1;
    int limit = 100;
    std::string dir;
    std::string file_name;  // set for --screenshot; empty means the normal naming
    int width = 1920;
    int height = 1080;
    int fps = 60;
    bool dump = false;
    bool window = false;
    std::string error;
};

// Validates the raw flags. On error, error is set and enabled is false.
CaptureOptions parseCaptureOptions(const CaptureFlags& flags);

}  // namespace capture

#endif  // CAPTURE_OPTIONS_H
