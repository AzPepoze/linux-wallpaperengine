#ifndef TRANSITION_AUDIO_H
#define TRANSITION_AUDIO_H

namespace lwe::transition {

// Decides the outgoing audio group's fate before loading; pure, so it is unit-testable.
struct AudioSwitchPlan {
    bool fade_old = false;           // mark the outgoing group fading and ramp it down
    bool destroy_old_now = false;    // hard cut: free the outgoing group immediately
    bool new_starts_silent = false;  // incoming group starts at 0 and ramps up
};

// selection is the transition effect; kSelectionNone means a hard cut.
AudioSwitchPlan planAudioSwitch(bool has_active_wallpaper, bool has_old_group, int selection);

// After a failed switch, destroy the outgoing group if its wallpaper was already cleared.
bool destroyOldGroupAfterFailure(bool has_active_wallpaper);

}  // namespace lwe::transition

#endif  // TRANSITION_AUDIO_H
