#include "wallpaper/transition/transition_catalog.h"

#include <cstdlib>

namespace lwe::transition {
namespace {

const EffectInfo kEffects[] = {
    {Effect::Fade, "fade", false},
    {Effect::Mosaic, "mosaic", false},
    {Effect::Diffuse, "diffuse", false},
    {Effect::HorizontalSlide, "horizontal_slide", false},
    {Effect::VerticalSlide, "vertical_slide", false},
    {Effect::HorizontalFade, "horizontal_fade", false},
    {Effect::VerticalFade, "vertical_fade", false},
    {Effect::Clouds, "clouds", false},
    {Effect::BurntPaper, "burnt_paper", false},
    {Effect::Circular, "circular", false},
    {Effect::Zipper, "zipper", false},
    {Effect::Door, "door", false},
    {Effect::Lines, "lines", false},
    {Effect::Zoom, "zoom", false},
    {Effect::Drip, "drip", false},
    {Effect::Pixelate, "pixelate", false},
    {Effect::Bricks, "bricks", true},
    {Effect::Paint, "paint", false},
    {Effect::FadeToBlack, "fade_to_black", false},
    {Effect::Twister, "twister", false},
    {Effect::BlackHole, "black_hole", false},
    {Effect::Crt, "crt", false},
    {Effect::RadialWipe, "radial_wipe", false},
    {Effect::GlassShatter, "glass_shatter", true},
    {Effect::Bullets, "bullets", false},
    {Effect::Ice, "ice", false},
    {Effect::Boilover, "boilover", false},
};

}  // namespace

const EffectInfo* effectByIndex(int index) {
    if (index < 0 || index >= (int)Effect::Count) return nullptr;
    return &kEffects[index];
}

const EffectInfo* effectByName(const std::string& name) {
    for (const EffectInfo& info : kEffects) {
        if (name == info.name) return &info;
    }
    if (name.empty()) return nullptr;
    char* end = nullptr;
    const long parsed = std::strtol(name.c_str(), &end, 10);
    if (end == name.c_str() || *end != '\0') return nullptr;
    return effectByIndex((int)parsed);
}

int effectCount() {
    return (int)Effect::Count;
}

bool parseSelection(const std::string& text, Selection& out) {
    if (text == "none") {
        out.value = kSelectionNone;
        return true;
    }
    if (text == "random") {
        out.value = kSelectionRandom;
        return true;
    }
    const EffectInfo* info = effectByName(text);
    if (!info) return false;
    out.value = (int)info->effect;
    return true;
}

int pickRandomEffect(uint32_t seed) {
    // splitmix32 finalizer: cheap, deterministic, and well distributed.
    seed += 0x9e3779b9u;
    seed = (seed ^ (seed >> 16)) * 0x21f0aaadu;
    seed = (seed ^ (seed >> 15)) * 0x735a2d97u;
    seed ^= seed >> 15;
    return (int)(seed % (uint32_t)effectCount());
}

float transitionProgress(float elapsed_seconds, int duration_ms) {
    if (duration_ms <= 0) return 1.0f;
    float progress = elapsed_seconds * 1000.0f / (float)duration_ms;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    return progress;
}

}  // namespace lwe::transition
