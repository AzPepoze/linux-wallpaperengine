#include <cjson/cJSON.h>

#include <cstdio>

#include "shared/assets/media/video_rate.h"
#include "test_util.h"
#include "wallpaper/video/video_properties.h"
using test::expect;

namespace {

VideoProperties parse(const char* json) {
    cJSON* root = cJSON_Parse(json);
    VideoProperties out = parseVideoProperties(root);
    cJSON_Delete(root);
    return out;
}

void testDefaults() {
    VideoProperties none = parse("{}");
    expect("defaults", none.rate == 1.0f && none.volume == 1.0f && none.fit == VideoFit::Default,
           "missing properties keep defaults");
    VideoProperties unrelated = parse(R"({"general":{"properties":{"schemecolor":{"type":"color","value":"0 0 0"}}}})");
    expect("defaults", unrelated.rate == 1.0f && unrelated.fit == VideoFit::Default, "unrelated keys are ignored");
}

void testRateAndVolume() {
    VideoProperties p = parse(
        R"({"general":{"properties":{"Rate":{"type":"slider","value":1.5},"volume":{"type":"slider","value":50,"max":100}}}})");
    expect("rate", p.rate == 1.5f, "rate value");
    expect("volume", p.volume > 0.499f && p.volume < 0.501f, "volume normalised by max");
    expect("rate", parse(R"({"general":{"properties":{"speed":{"value":"100"}}}})").rate == kMaxPlaybackRate,
           "rate is clamped");
    expect("volume", parse(R"({"general":{"properties":{"volume":{"value":0.3}}}})").volume == 0.3f,
           "unit volume kept");
}

void testFit() {
    const char* combo =
        R"({"general":{"properties":{"scaling":{"type":"combo","value":"1","options":[{"label":"Fit","value":"0"},{"label":"Fill","value":"1"}]}}}})";
    expect("fit", parse(combo).fit == VideoFit::Fill, "string combo resolves to label");
    const char* numeric =
        R"({"general":{"properties":{"fit":{"type":"combo","value":0,"options":[{"label":"Fit","value":0},{"label":"Fill","value":1}]}}}})";
    expect("fit", parse(numeric).fit == VideoFit::Fit, "numeric combo resolves to label");
    expect("fit", parse(R"({"general":{"properties":{"fit":{"value":"cover"}}}})").fit == VideoFit::Fill,
           "plain value is classified");
    expect("fit", parse(R"({"general":{"properties":{"fit":{"value":"stretch"}}}})").fit == VideoFit::Default,
           "unsupported mode keeps the default");
}

void testRateMath() {
    expect("rate math", resampledAudioRate(48000, 1.0f) == 48000, "unit rate keeps the rate");
    expect("rate math", resampledAudioRate(48000, 2.0f) == 24000, "double speed halves the sample count");
    expect("rate math", resampledAudioRate(48000, 0.5f) == 96000, "half speed doubles the sample count");
    expect("rate math", clampPlaybackRate(0.0f) == kMinPlaybackRate, "rate clamps low");
    expect("rate math", clampPlaybackRate(1.0f / 0.0f) == 1.0f, "non-finite rate falls back");
}

}  // namespace

int main() {
    testDefaults();
    testRateAndVolume();
    testFit();
    testRateMath();
    return test::finish("video tests");
}
