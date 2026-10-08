#include "app/platform/pointer/global_pointer.h"

#include "shared/core/logger.h"

void GlobalPointer::open(PointerSource source) {
    use_evdev_ = source != PointerSource::Surface && evdev_.open();
    if (use_evdev_) {
        LOG_TAG_I("POINTER", "Global pointer: evdev mouse motion");
    } else if (source == PointerSource::Evdev) {
        LOG_TAG_W("POINTER", "No readable mouse in /dev/input (needs the input group); using the surface only");
    }
}

OutputPointer GlobalPointer::poll(OutputPointer current, float width, float height) {
    // Without a readable mouse, the surface is the only source, and it reports only while the pointer is over it.
    if (!use_evdev_) return current;

    // evdev is the primary source: motion is integrated from the last position it produced. The surface's own
    // pointer events are ignored while evdev works, so the surface is only a last resort.
    const OutputPointer base = has_last_ ? last_ : current;
    last_ = evdev_.advance(base, width, height);
    has_last_ = true;
    return last_;
}
