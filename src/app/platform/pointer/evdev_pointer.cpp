#include "app/platform/pointer/evdev_pointer.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <string>

namespace {
bool hasBit(const unsigned char* bits, int code) {
    return (bits[code / 8] & (1u << (code % 8))) != 0;
}

// A mouse moves on REL_X/REL_Y and has a left button; keyboards and touch devices are skipped.
bool isMouse(int fd) {
    unsigned char rel[(REL_MAX / 8) + 1] = {};
    unsigned char keys[(KEY_MAX / 8) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel) < 0) return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) return false;
    return hasBit(rel, REL_X) && hasBit(rel, REL_Y) && hasBit(keys, BTN_LEFT);
}
}  // namespace

OutputPointer movedBy(OutputPointer from, int dx, int dy, float width, float height) {
    OutputPointer out = from;
    out.x = std::clamp(from.x + (float)dx / width, 0.0f, 1.0f);
    out.y = std::clamp(from.y + (float)dy / height, 0.0f, 1.0f);
    out.inside = true;
    return out;
}

EvdevPointer::~EvdevPointer() {
    for (int fd : fds_) close(fd);
}

bool EvdevPointer::open() {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input", error)) {
        if (entry.path().filename().string().rfind("event", 0) != 0) continue;
        const int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (isMouse(fd)) {
            fds_.push_back(fd);
        } else {
            close(fd);
        }
    }
    return !fds_.empty();
}

OutputPointer EvdevPointer::advance(OutputPointer from, float width, float height) {
    if (fds_.empty() || width <= 0.0f || height <= 0.0f) return from;
    int dx = 0, dy = 0;
    for (auto it = fds_.begin(); it != fds_.end();) {
        input_event event;
        bool gone = false;
        errno = 0;
        while (::read(*it, &event, sizeof(event)) == sizeof(event)) {
            if (event.type != EV_REL) continue;
            if (event.code == REL_X) dx += event.value;
            if (event.code == REL_Y) dy += event.value;
        }
        // A read that fails with ENODEV means the device was unplugged; drop it.
        if (errno == ENODEV) {
            gone = true;
            close(*it);
        }
        it = gone ? fds_.erase(it) : it + 1;
    }
    return movedBy(from, dx, dy, width, height);
}
