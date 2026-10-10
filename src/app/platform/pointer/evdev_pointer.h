#ifndef EVDEV_POINTER_H
#define EVDEV_POINTER_H

#include <vector>

#include "app/platform/pointer/output_pointer.h"

// Applies relative pixel motion to `from`, scaled to and clamped within the output dimensions.
OutputPointer movedBy(OutputPointer from, int dx, int dy, float width, float height);

// Follows relative motion from mouse-like devices in /dev/input. Works on any compositor, but only
// when the user can read those devices (the input group or a udev rule). Key events are never read.
class EvdevPointer {
   public:
    ~EvdevPointer();
    EvdevPointer() = default;
    EvdevPointer(const EvdevPointer&) = delete;
    EvdevPointer& operator=(const EvdevPointer&) = delete;

    // Opens every readable mouse; false when none can be read.
    bool open();
    // Moves `from` by the motion read since the last call.
    OutputPointer advance(OutputPointer from, float width, float height);

   private:
    std::vector<int> fds_;
};

#endif  // EVDEV_POINTER_H
