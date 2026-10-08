#include "app/platform/pointer/global_pointer.h"

#include "app/platform/pointer/exact_pointer.h"
#include "shared/core/logger.h"

void GlobalPointer::open(const std::string& output, PointerSource source) {
    exact_ = nullptr;
    if (const std::optional<ExactSource> exact = findExactSource(output, source)) {
        exact_ = exact->sample;
        exact_name_ = exact->name;
        LOG_TAG_I("POINTER", "Global pointer: %s (exact)", exact->name.c_str());
    } else if (source == PointerSource::X11 || source == PointerSource::Hyprland) {
        LOG_TAG_W("POINTER", "The requested pointer source is not available here; using the next one");
    }

    use_evdev_ = source != PointerSource::Surface && evdev_.open();
    // The surface stays on unless evdev was asked for alone and can be read.
    use_surface_ = source != PointerSource::Evdev || !use_evdev_;
    if (!exact_ && use_evdev_) LOG_TAG_I("POINTER", "Global pointer: evdev mouse motion (estimate)");
    if (!exact_ && !use_evdev_ && source == PointerSource::Evdev)
        LOG_TAG_W("POINTER", "No readable mouse in /dev/input (needs the input group); using the surface only");
}

OutputPointer GlobalPointer::poll(OutputPointer current, bool over_surface, float width, float height) {
    if (exact_) {
        const std::optional<OutputPointer> exact = exact_();
        if (exact && exact->inside) return remember(*exact, exact_name_);
    }
    // The surface reports the pointer exactly while it is over the wallpaper.
    if (use_surface_ && over_surface) return remember(current, "surface");
    if (use_evdev_) {
        const OutputPointer base = has_last_ ? last_ : current;
        return remember(evdev_.advance(base, width, height), "evdev");
    }
    source_ = "none";
    return current;
}

void GlobalPointer::setExactSampler(std::function<std::optional<OutputPointer>()> sampler) {
    exact_ = std::move(sampler);
}

OutputPointer GlobalPointer::remember(OutputPointer position, const std::string& name) {
    source_ = name;
    last_ = position;
    has_last_ = true;
    return position;
}
