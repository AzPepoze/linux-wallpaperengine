#include "shared/audio/audio_engine.h"

#include "test_util.h"

int main() {
    AudioEngine& audio = AudioEngine::instance();

    // Groups are pure bookkeeping and work with no audio device open.
    CHECK(audio.groupVolume(AudioEngine::kDefaultGroup) == 1.0f);

    const AudioEngine::GroupId g1 = audio.createGroup();
    const AudioEngine::GroupId g2 = audio.createGroup();
    CHECK(g1 != AudioEngine::kDefaultGroup);
    CHECK(g2 != AudioEngine::kDefaultGroup);
    CHECK(g1 != g2);

    audio.setGroupVolume(g1, 0.25f);
    CHECK(audio.groupVolume(g1) == 0.25f);
    CHECK(audio.groupVolume(g2) == 1.0f);

    CHECK(!audio.groupFading(g1));
    audio.beginGroupFade(g1);
    CHECK(audio.groupFading(g1));
    audio.cancelGroupFade(g1);
    CHECK(!audio.groupFading(g1));

    audio.destroyGroup(g1);
    audio.destroyGroup(AudioEngine::kDefaultGroup);  // must be a no-op
    CHECK(audio.groupVolume(AudioEngine::kDefaultGroup) == 1.0f);

    return test::finish("audio engine group checks");
}
