#ifndef ALPHA_CURVE_H
#define ALPHA_CURVE_H

#include "wallpaper/2d/parser/scene_document.h"

float evaluateImageAlpha(const wallpaper_engine::ImageObjectDocument& image, float time_seconds);

#endif  // ALPHA_CURVE_H
