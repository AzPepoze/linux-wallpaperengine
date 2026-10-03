#include "alpha_curve.h"

#include "animation_curve.h"

float evaluateImageAlpha(const wallpaper_engine::ImageObjectDocument& image, float time_seconds) {
    if (image.alpha_keys.empty()) return image.alpha;
    return evaluateCurve(image.alpha_keys, image.alpha_fps, image.alpha_length, image.alpha_mode, time_seconds);
}
