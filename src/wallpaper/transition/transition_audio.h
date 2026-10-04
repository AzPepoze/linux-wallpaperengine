#ifndef TRANSITION_AUDIO_H
#define TRANSITION_AUDIO_H

namespace lwe::transition {

// What a switch does to the outgoing audio group, decided before loading the
// incoming wallpaper. Kept pure so the lifecycle rules can be unit tested.
struct AudioSwitchPlan {
    bool fade_old = false;           // mark the outgoing group fading and ramp it down
    bool destroy_old_now = false;    // hard cut: free the outgoing group immediately
    bool new_starts_silent = false;  // incoming group starts at 0 and ramps up
};

// has_active_wallpaper: a wallpaper is currently loaded.
// has_old_group: that wallpaper owns a non-default audio group.
// selection: the transition effect; kSelectionNone means a hard cut.
AudioSwitchPlan planAudioSwitch(bool has_active_wallpaper, bool has_old_group, int selection);

// After a failed switch, whether the outgoing group must be destroyed rather
// than restored. True when the outgoing wallpaper was already cleared, so no
// owner remains for the voices detached by the fade.
bool destroyOldGroupAfterFailure(bool has_active_wallpaper);

}  // namespace lwe::transition

#endif  // TRANSITION_AUDIO_H
