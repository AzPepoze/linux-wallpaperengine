#include "image_stats.h"

#include <algorithm>
#include <cmath>

ImageStats ImageStats::compute(const uint8_t* rgba_data, int width, int height) {
    ImageStats stats;
    stats.hist_r.assign(16, 0);
    stats.hist_g.assign(16, 0);
    stats.hist_b.assign(16, 0);
    stats.hist_a.assign(16, 0);

    if (!rgba_data || width <= 0 || height <= 0) return stats;

    const size_t total_pixels = (size_t)width * (size_t)height;
    double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0, sum_a = 0.0;
    double sum_lum = 0.0;
    double sum_sat = 0.0;

    for (int i = 0; i < 4; ++i) {
        stats.min_rgba[i] = 1.0f;
        stats.max_rgba[i] = 0.0f;
    }

    for (size_t i = 0; i < total_pixels; ++i) {
        const uint8_t r_byte = rgba_data[i * 4 + 0];
        const uint8_t g_byte = rgba_data[i * 4 + 1];
        const uint8_t b_byte = rgba_data[i * 4 + 2];
        const uint8_t a_byte = rgba_data[i * 4 + 3];

        const float rf = (float)r_byte / 255.0f;
        const float gf = (float)g_byte / 255.0f;
        const float bf = (float)b_byte / 255.0f;
        const float af = (float)a_byte / 255.0f;

        sum_r += rf;
        sum_g += gf;
        sum_b += bf;
        sum_a += af;

        stats.min_rgba[0] = std::min(stats.min_rgba[0], rf);
        stats.min_rgba[1] = std::min(stats.min_rgba[1], gf);
        stats.min_rgba[2] = std::min(stats.min_rgba[2], bf);
        stats.min_rgba[3] = std::min(stats.min_rgba[3], af);

        stats.max_rgba[0] = std::max(stats.max_rgba[0], rf);
        stats.max_rgba[1] = std::max(stats.max_rgba[1], gf);
        stats.max_rgba[2] = std::max(stats.max_rgba[2], bf);
        stats.max_rgba[3] = std::max(stats.max_rgba[3], af);

        // Luminance (ITU-R BT.709)
        const float lum = 0.2126f * rf + 0.7152f * gf + 0.0722f * bf;
        sum_lum += lum;

        // Saturation (HSV)
        const float max_c = std::max({rf, gf, bf});
        const float min_c = std::min({rf, gf, bf});
        const float delta_c = max_c - min_c;
        const float sat = (max_c > 1e-6f) ? (delta_c / max_c) : 0.0f;
        sum_sat += sat;

        stats.hist_r[std::min(15, (int)(r_byte >> 4))]++;
        stats.hist_g[std::min(15, (int)(g_byte >> 4))]++;
        stats.hist_b[std::min(15, (int)(b_byte >> 4))]++;
        stats.hist_a[std::min(15, (int)(a_byte >> 4))]++;
    }

    stats.mean_rgba[0] = (float)(sum_r / (double)total_pixels);
    stats.mean_rgba[1] = (float)(sum_g / (double)total_pixels);
    stats.mean_rgba[2] = (float)(sum_b / (double)total_pixels);
    stats.mean_rgba[3] = (float)(sum_a / (double)total_pixels);
    stats.mean_luminance = (float)(sum_lum / (double)total_pixels);
    stats.mean_saturation = (float)(sum_sat / (double)total_pixels);

    return stats;
}

cJSON* ImageStats::toJson() const {
    cJSON* root = cJSON_CreateObject();

    cJSON* mean_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; ++i) cJSON_AddItemToArray(mean_arr, cJSON_CreateNumber(mean_rgba[i]));
    cJSON_AddItemToObject(root, "mean_rgba", mean_arr);

    cJSON* min_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; ++i) cJSON_AddItemToArray(min_arr, cJSON_CreateNumber(min_rgba[i]));
    cJSON_AddItemToObject(root, "min_rgba", min_arr);

    cJSON* max_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; ++i) cJSON_AddItemToArray(max_arr, cJSON_CreateNumber(max_rgba[i]));
    cJSON_AddItemToObject(root, "max_rgba", max_arr);

    cJSON_AddNumberToObject(root, "mean_luminance", mean_luminance);
    cJSON_AddNumberToObject(root, "mean_saturation", mean_saturation);

    cJSON* hist_obj = cJSON_CreateObject();
    auto add_hist = [](cJSON* obj, const char* name, const std::vector<uint32_t>& hist) {
        cJSON* arr = cJSON_CreateArray();
        for (uint32_t count : hist) cJSON_AddItemToArray(arr, cJSON_CreateNumber(count));
        cJSON_AddItemToObject(obj, name, arr);
    };
    add_hist(hist_obj, "r", hist_r);
    add_hist(hist_obj, "g", hist_g);
    add_hist(hist_obj, "b", hist_b);
    add_hist(hist_obj, "a", hist_a);
    cJSON_AddItemToObject(root, "histogram_16bin", hist_obj);

    return root;
}

ImageDeltaStats ImageDeltaStats::compute(const uint8_t* a, const uint8_t* b, int width, int height) {
    ImageDeltaStats delta;
    if (!a || !b || width <= 0 || height <= 0) return delta;

    const size_t total_pixels = (size_t)width * (size_t)height;
    double sum_dr = 0.0, sum_dg = 0.0, sum_db = 0.0, sum_da = 0.0;
    double sum_abs_rgb = 0.0;
    double max_abs_rgb = 0.0;
    double sum_sq_rgb = 0.0;
    double sum_lum_delta = 0.0;
    double sum_sat_delta = 0.0;

    for (size_t i = 0; i < total_pixels; ++i) {
        const float ar = (float)a[i * 4 + 0] / 255.0f;
        const float ag = (float)a[i * 4 + 1] / 255.0f;
        const float ab = (float)a[i * 4 + 2] / 255.0f;
        const float aa = (float)a[i * 4 + 3] / 255.0f;

        const float br = (float)b[i * 4 + 0] / 255.0f;
        const float bg = (float)b[i * 4 + 1] / 255.0f;
        const float bb = (float)b[i * 4 + 2] / 255.0f;
        const float ba = (float)b[i * 4 + 3] / 255.0f;

        const float dr = ar - br;
        const float dg = ag - bg;
        const float db = ab - bb;
        const float da = aa - ba;

        sum_dr += dr;
        sum_dg += dg;
        sum_db += db;
        sum_da += da;

        const float abs_r = std::abs(dr);
        const float abs_g = std::abs(dg);
        const float abs_b = std::abs(db);
        const float pixel_max_abs = std::max({abs_r, abs_g, abs_b});
        max_abs_rgb = std::max(max_abs_rgb, (double)pixel_max_abs);
        sum_abs_rgb += (abs_r + abs_g + abs_b) / 3.0;

        sum_sq_rgb += (dr * dr + dg * dg + db * db) / 3.0;

        const float lum_a = 0.2126f * ar + 0.7152f * ag + 0.0722f * ab;
        const float lum_b = 0.2126f * br + 0.7152f * bg + 0.0722f * bb;
        sum_lum_delta += (lum_a - lum_b);

        const float max_a = std::max({ar, ag, ab});
        const float min_a = std::min({ar, ag, ab});
        const float sat_a = (max_a > 1e-6f) ? ((max_a - min_a) / max_a) : 0.0f;

        const float max_b = std::max({br, bg, bb});
        const float min_b = std::min({br, bg, bb});
        const float sat_b = (max_b > 1e-6f) ? ((max_b - min_b) / max_b) : 0.0f;
        sum_sat_delta += (sat_a - sat_b);
    }

    delta.mean_delta_rgba[0] = (float)(sum_dr / (double)total_pixels);
    delta.mean_delta_rgba[1] = (float)(sum_dg / (double)total_pixels);
    delta.mean_delta_rgba[2] = (float)(sum_db / (double)total_pixels);
    delta.mean_delta_rgba[3] = (float)(sum_da / (double)total_pixels);

    delta.mean_abs_delta_rgb = (float)(sum_abs_rgb / (double)total_pixels);
    delta.max_abs_delta_rgb = (float)max_abs_rgb;
    delta.luminance_delta = (float)(sum_lum_delta / (double)total_pixels);
    delta.saturation_delta = (float)(sum_sat_delta / (double)total_pixels);

    const double mse = sum_sq_rgb / (double)total_pixels;
    if (mse < 1e-10) {
        delta.psnr_rgb = 999.0f;  // infinite/lossless
    } else {
        delta.psnr_rgb = (float)(10.0 * std::log10(1.0 / mse));
    }

    return delta;
}

ImageDeltaStats ImageDeltaStats::computeFromStats(const ImageStats& current, const ImageStats& reference) {
    ImageDeltaStats delta;
    for (int i = 0; i < 4; ++i) {
        delta.mean_delta_rgba[i] = current.mean_rgba[i] - reference.mean_rgba[i];
    }
    delta.mean_abs_delta_rgb =
        (std::abs(delta.mean_delta_rgba[0]) + std::abs(delta.mean_delta_rgba[1]) + std::abs(delta.mean_delta_rgba[2])) /
        3.0f;
    delta.luminance_delta = current.mean_luminance - reference.mean_luminance;
    delta.saturation_delta = current.mean_saturation - reference.mean_saturation;
    return delta;
}

cJSON* ImageDeltaStats::toJson() const {
    cJSON* root = cJSON_CreateObject();

    cJSON* mean_delta = cJSON_CreateObject();
    cJSON_AddNumberToObject(mean_delta, "mean_r", mean_delta_rgba[0]);
    cJSON_AddNumberToObject(mean_delta, "mean_g", mean_delta_rgba[1]);
    cJSON_AddNumberToObject(mean_delta, "mean_b", mean_delta_rgba[2]);
    cJSON_AddNumberToObject(mean_delta, "mean_a", mean_delta_rgba[3]);
    cJSON_AddItemToObject(root, "mean_delta_rgba", mean_delta);

    cJSON_AddNumberToObject(root, "mean_abs_delta_rgb", mean_abs_delta_rgb);
    cJSON_AddNumberToObject(root, "max_abs_delta_rgb", max_abs_delta_rgb);
    cJSON_AddNumberToObject(root, "luminance_delta", luminance_delta);
    cJSON_AddNumberToObject(root, "saturation_delta", saturation_delta);
    if (psnr_rgb > 0.0f) {
        cJSON_AddNumberToObject(root, "psnr_rgb_db", psnr_rgb);
    }

    return root;
}
