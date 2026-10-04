#ifndef TRANSITION_SHADER_H
#define TRANSITION_SHADER_H

#include "wallpaper/transition/transition_catalog.h"

// What the compositor should play for one switch.
struct TransitionConfig {
    int selection = (int)lwe::transition::Effect::Fade;
    int duration_ms = 1000;
};

#endif  // TRANSITION_SHADER_H
