#include "app/platform/pointer/pointer_source.h"

bool parsePointerSource(const std::string& text, PointerSource& out) {
    if (text == "auto") {
        out = PointerSource::Auto;
    } else if (text == "surface") {
        out = PointerSource::Surface;
    } else if (text == "evdev") {
        out = PointerSource::Evdev;
    } else {
        return false;
    }
    return true;
}
