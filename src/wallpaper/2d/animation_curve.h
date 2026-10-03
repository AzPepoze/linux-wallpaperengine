#ifndef ANIMATION_CURVE_H
#define ANIMATION_CURVE_H

#include <string>
#include <vector>

namespace wallpaper_engine {

struct CurveKeyframe {
    float frame = 0.0f;
    float value = 1.0f;
};

struct AnimationCurve {
    std::vector<CurveKeyframe> keys;
    float fps = 30.0f;
    float length = 0.0f;
    std::string mode;
};

}  // namespace wallpaper_engine

// Evaluates a Wallpaper Engine keyframe curve at `time_seconds`. `mode` follows
// timeline semantics: "single" plays once and holds the last key, "mirror"
// ping-pongs, anything else loops.
float evaluateCurve(const std::vector<wallpaper_engine::CurveKeyframe>& keys, float fps, float length,
                    const std::string& mode, float time_seconds);

#endif  // ANIMATION_CURVE_H
