#include "shared/audio/audio_engine.h"

#include <vector>

#include "test_util.h"

int main() {
    AudioEngine& audio = AudioEngine::instance();

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
    audio.destroyGroup(AudioEngine::kDefaultGroup);
    CHECK(audio.groupVolume(AudioEngine::kDefaultGroup) == 1.0f);

    const AudioEngine::StreamHandle stream = audio.createStream(48000, 2);
    CHECK(stream != AudioEngine::kInvalidStream);
    const std::vector<float> frames((25000 + 1024) * 2, 0.0f);
    audio.pushStream(stream, frames.data(), (uint32_t)(frames.size() / 2));
    CHECK(audio.streamDroppedFrames(stream) == 0);
    CHECK(audio.streamQueuedFrames(stream) >= 24000);
    audio.destroyStream(stream);

    audio.setCaptureEnabled(false);
    CHECK(!audio.hasCapture());
    audio.update(1.0f / 60.0f);
    CHECK(audio.spectrum().bands64_left[0] == 0.0f);
    audio.setCaptureEnabled(false);
    CHECK(!audio.hasCapture());

    return test::finish("audio engine group checks");
}
