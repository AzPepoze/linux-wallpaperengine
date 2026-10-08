#ifndef GLOBAL_POINTER_H
#define GLOBAL_POINTER_H

#include "app/platform/pointer/evdev_pointer.h"
#include "app/platform/pointer/output_pointer.h"
#include "app/platform/pointer/pointer_source.h"

// Tracks the pointer on this output across the whole desktop: evdev first, the wallpaper surface as last resort.
class GlobalPointer {
   public:
    void open(PointerSource source);
    // `current` is the position the engine holds now; it is used only when no mouse device can be read.
    OutputPointer poll(OutputPointer current, float width, float height);

   private:
    bool use_evdev_ = false;
    EvdevPointer evdev_;
    OutputPointer last_;
    bool has_last_ = false;
};

#endif  // GLOBAL_POINTER_H
