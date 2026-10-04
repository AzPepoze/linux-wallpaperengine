#include "wallpaper/transition/transition_audio.h"

#include "wallpaper/transition/transition_catalog.h"

namespace lwe::transition {

AudioSwitchPlan planAudioSwitch(bool has_active_wallpaper, bool has_old_group, int selection) {
    AudioSwitchPlan plan;
    if (!has_active_wallpaper || !has_old_group) return plan;
    if (selection == kSelectionNone) {
        plan.destroy_old_now = true;
        return plan;
    }
    plan.fade_old = true;
    plan.new_starts_silent = true;
    return plan;
}

bool destroyOldGroupAfterFailure(bool has_active_wallpaper) {
    return !has_active_wallpaper;
}

}  // namespace lwe::transition
