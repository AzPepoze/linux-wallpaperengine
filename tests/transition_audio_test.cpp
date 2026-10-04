#include "test_util.h"
#include "wallpaper/transition/transition_audio.h"
#include "wallpaper/transition/transition_catalog.h"

using namespace lwe::transition;

int main() {
    // No active wallpaper: nothing to fade or cut; incoming starts at full.
    AudioSwitchPlan none_active = planAudioSwitch(false, false, (int)Effect::Fade);
    CHECK(!none_active.fade_old && !none_active.destroy_old_now && !none_active.new_starts_silent);

    // Active wallpaper but no own group yet: no crossfade.
    AudioSwitchPlan no_group = planAudioSwitch(true, false, (int)Effect::Fade);
    CHECK(!no_group.fade_old && !no_group.destroy_old_now && !no_group.new_starts_silent);

    // Hard cut: destroy the outgoing group now; incoming starts at full.
    AudioSwitchPlan hard_cut = planAudioSwitch(true, true, kSelectionNone);
    CHECK(!hard_cut.fade_old && hard_cut.destroy_old_now && !hard_cut.new_starts_silent);

    // Crossfade: fade the outgoing; incoming starts silent.
    AudioSwitchPlan crossfade = planAudioSwitch(true, true, (int)Effect::Fade);
    CHECK(crossfade.fade_old && !crossfade.destroy_old_now && crossfade.new_starts_silent);

    // Failed switch: destroy the old group only when its wallpaper was cleared.
    CHECK(!destroyOldGroupAfterFailure(true));
    CHECK(destroyOldGroupAfterFailure(false));
    return test::finish("transition audio plan checks");
}
