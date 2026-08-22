#ifndef IMAGE_STATS_H
#define IMAGE_STATS_H

#include <cjson/cJSON.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

struct ImageStats {
    float mean_rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float min_rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float max_rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float mean_luminance = 0.0f;
    float mean_saturation = 0.0f;

    std::vector<uint32_t> hist_r;  // 16 bins
    std::vector<uint32_t> hist_g;  // 16 bins
    std::vector<uint32_t> hist_b;  // 16 bins
    std::vector<uint32_t> hist_a;  // 16 bins

    static ImageStats compute(const uint8_t* rgba_data, int width, int height);
    cJSON* toJson() const;
};

struct ImageDeltaStats {
    float mean_delta_rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float mean_abs_delta_rgb = 0.0f;
    float max_abs_delta_rgb = 0.0f;
    float luminance_delta = 0.0f;
    float saturation_delta = 0.0f;
    float psnr_rgb = 0.0f;  // dB, or inf if identical

    static ImageDeltaStats compute(const uint8_t* a, const uint8_t* b, int width, int height);
    static ImageDeltaStats computeFromStats(const ImageStats& current, const ImageStats& reference);
    cJSON* toJson() const;
};

#endif  // IMAGE_STATS_H
