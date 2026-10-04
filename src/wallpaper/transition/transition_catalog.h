#ifndef TRANSITION_CATALOG_H
#define TRANSITION_CATALOG_H

#include <cstdint>
#include <string>

namespace lwe::transition {

// Wallpaper Engine's playlist transition effects (FADEEFFECT combo values).
enum class Effect : int {
    Fade = 0,
    Mosaic,
    Diffuse,
    HorizontalSlide,
    VerticalSlide,
    HorizontalFade,
    VerticalFade,
    Clouds,
    BurntPaper,
    Circular,
    Zipper,
    Door,
    Lines,
    Zoom,
    Drip,
    Pixelate,
    Bricks,
    Paint,
    FadeToBlack,
    Twister,
    BlackHole,
    Crt,
    RadialWipe,
    GlassShatter,
    Bullets,
    Ice,
    Boilover,
    Count,
};

struct EffectInfo {
    Effect effect;
    const char* name;
    bool needs_geometry;
};

const EffectInfo* effectByIndex(int index);
const EffectInfo* effectByName(const std::string& name);
int effectCount();

// Selection sentinels used outside the 0..26 effect range.
constexpr int kSelectionNone = -1;
constexpr int kSelectionRandom = -2;

struct Selection {
    int value = (int)Effect::Fade;
};

// Accepts an effect name ("fade"), a decimal index ("0".."26"), "none" or "random".
bool parseSelection(const std::string& text, Selection& out);

// Deterministic pick in 0..26 for a given seed; the same seed always returns the same effect.
int pickRandomEffect(uint32_t seed);

// Linear 0..1 progress for elapsed_seconds over duration_ms; 1.0 when the duration is non-positive.
float transitionProgress(float elapsed_seconds, int duration_ms);

// Resolves a raw `--transition` string. Empty means fade. Sets out_duration_ms to
// 1000 when it is non-positive, and fills `error` on an unknown value.
bool resolveTransitionSetting(const std::string& raw, int& out_value, int& out_duration_ms, std::string& error);

}  // namespace lwe::transition

#endif  // TRANSITION_CATALOG_H
