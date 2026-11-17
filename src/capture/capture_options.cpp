#include "capture/capture_options.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace capture {
namespace {

constexpr long kMaxFps = 240;
constexpr const char* kNeedsTrigger = "--capture-from needs --capture or --capture-every";
constexpr const char* kNeedsScreenshot = "--screenshot-delay needs --screenshot";

bool parseNumber(const std::string& text, double& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(value);
}

bool parseIntInRange(const std::string& text, long min, long max, int& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end != text.c_str() + text.size()) return false;
    if (parsed < min || parsed > max) return false;
    value = static_cast<int>(parsed);
    return true;
}

std::vector<std::string> splitList(const std::string& text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end - start));
        if (end == std::string::npos) return parts;
        start = end + 1;
    }
}

bool parseTimes(const std::string& text, std::vector<double>& times) {
    std::vector<double> parsed;
    for (const std::string& part : splitList(text, ',')) {
        double value = 0;
        if (!parseNumber(part, value) || value < 0) return false;
        parsed.push_back(value);
    }
    std::sort(parsed.begin(), parsed.end());
    parsed.erase(std::unique(parsed.begin(), parsed.end()), parsed.end());
    times = std::move(parsed);
    return true;
}

std::string invalid(const char* flag, const std::string& value, const char* problem) {
    return std::string(flag) + ": '" + value + "' " + problem;
}

bool hasTrigger(const CaptureFlags& flags) {
    return !flags.capture.empty() || !flags.every.empty();
}

bool hasScreenshot(const CaptureFlags& flags) {
    return !flags.screenshot.empty();
}

// Folder part of a path; "." when there is none, "/" for a file in the root.
std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string fileNameOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool hasSettingFlags(const CaptureFlags& flags) {
    return !flags.from.empty() || !flags.until.empty() || !flags.limit.empty() || !flags.dir.empty() ||
           !flags.size.empty() || !flags.fps.empty() || flags.dump || flags.window;
}

std::string parseTriggers(const CaptureFlags& flags, CaptureOptions& options) {
    if (!flags.capture.empty() && !parseTimes(flags.capture, options.at_times)) {
        return invalid("--capture", flags.capture, "is not a list of times in seconds");
    }
    if (flags.every == "frame") {
        options.every_frame = true;
    } else if (!flags.every.empty()) {
        if (!parseNumber(flags.every, options.every_seconds) || options.every_seconds <= 0) {
            return invalid("--capture-every", flags.every, "is not a positive number of seconds");
        }
    }
    return {};
}

std::string parseTiming(const CaptureFlags& flags, CaptureOptions& options) {
    if (!flags.from.empty()) {
        if (!parseNumber(flags.from, options.from) || options.from < 0) {
            return invalid("--capture-from", flags.from, "is not a time of zero or more seconds");
        }
    }
    if (!flags.until.empty()) {
        double until = 0;
        if (!parseNumber(flags.until, until) || until <= options.from) {
            return invalid("--capture-until", flags.until, "must be a time after --capture-from");
        }
        options.until = until;
    }
    return {};
}

std::string parseLimit(const CaptureFlags& flags, CaptureOptions& options) {
    if (flags.limit.empty()) return {};
    const long max = std::numeric_limits<int>::max();
    if (!parseIntInRange(flags.limit, 1, max, options.limit)) {
        return invalid("--capture-limit", flags.limit, "is not a positive whole number");
    }
    return {};
}

std::string parseSize(const CaptureFlags& flags, CaptureOptions& options) {
    if (flags.size.empty()) return {};
    const long max = std::numeric_limits<int>::max();
    const size_t split = flags.size.find('x');
    if (split == std::string::npos ||
        !parseIntInRange(flags.size.substr(0, split), 1, max, options.width) ||
        !parseIntInRange(flags.size.substr(split + 1), 1, max, options.height)) {
        return invalid("--capture-size", flags.size, "must look like 1920x1080");
    }
    return {};
}

std::string parseFps(const CaptureFlags& flags, CaptureOptions& options) {
    if (flags.fps.empty()) return {};
    if (!parseIntInRange(flags.fps, 1, kMaxFps, options.fps)) {
        return invalid("--capture-fps", flags.fps, "is not a whole number from 1 to 240");
    }
    return {};
}

std::string parseScreenshotDelay(const CaptureFlags& flags, double& delay) {
    if (flags.screenshot_delay.empty()) return {};
    if (!parseNumber(flags.screenshot_delay, delay) || delay < 0) {
        return invalid("--screenshot-delay", flags.screenshot_delay, "is not a time of zero or more seconds");
    }
    return {};
}

// One shot at the exact path; it only takes the size and fps settings.
std::string parseScreenshot(const CaptureFlags& flags, CaptureOptions& options) {
    if (hasTrigger(flags)) return "--screenshot cannot be combined with --capture or --capture-every";
    if (!flags.from.empty() || !flags.until.empty() || !flags.limit.empty() || !flags.dir.empty() || flags.dump) {
        return "--screenshot only takes --screenshot-delay, --capture-size and --capture-fps";
    }
    options.file_name = fileNameOf(flags.screenshot);
    if (options.file_name.empty()) return invalid("--screenshot", flags.screenshot, "must end in a file name");
    double delay = 0;
    std::string error = parseScreenshotDelay(flags, delay);
    if (!error.empty()) return error;
    options.at_times = {delay};
    options.limit = 1;
    options.dir = folderOf(flags.screenshot);
    error = parseSize(flags, options);
    if (!error.empty()) return error;
    return parseFps(flags, options);
}

using Step = std::string (*)(const CaptureFlags&, CaptureOptions&);

std::string parseInto(const CaptureFlags& flags, CaptureOptions& options) {
    if (hasScreenshot(flags)) return parseScreenshot(flags, options);
    if (!flags.screenshot_delay.empty()) return kNeedsScreenshot;
    if (!hasTrigger(flags)) {
        if (hasSettingFlags(flags)) return kNeedsTrigger;
        return {};
    }
    constexpr Step kSteps[] = {parseTriggers, parseTiming, parseLimit, parseSize, parseFps};
    for (Step step : kSteps) {
        std::string error = step(flags, options);
        if (!error.empty()) return error;
    }
    options.dir = flags.dir;
    options.dump = flags.dump;
    options.window = flags.window;
    return {};
}

}  // namespace

CaptureOptions parseCaptureOptions(const CaptureFlags& flags) {
    CaptureOptions options;
    options.error = parseInto(flags, options);
    options.enabled = options.error.empty() && (hasTrigger(flags) || hasScreenshot(flags));
    return options;
}

}  // namespace capture
