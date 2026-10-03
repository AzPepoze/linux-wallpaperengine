#include "wallpaper/video/video_properties.h"

#include <strings.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include "shared/assets/media/video_rate.h"

namespace {
const cJSON* findProperty(const cJSON* properties, std::initializer_list<const char*> names) {
    for (const cJSON* item = properties->child; item; item = item->next) {
        if (!item->string || !cJSON_IsObject(item)) continue;
        for (const char* name : names) {
            if (strcasecmp(item->string, name) == 0) return item;
        }
    }
    return nullptr;
}

bool numberValue(const cJSON* value, double& out) {
    if (cJSON_IsNumber(value)) {
        out = value->valuedouble;
        return true;
    }
    if (cJSON_IsString(value) && value->valuestring && value->valuestring[0] != '\0') {
        char* end = nullptr;
        out = strtod(value->valuestring, &end);
        return end && *end == '\0';
    }
    return false;
}

bool numberProperty(const cJSON* property, double& out) {
    return property && numberValue(cJSON_GetObjectItemCaseSensitive(property, "value"), out);
}

float volumeFrom(const cJSON* property, float fallback) {
    double value = 0.0;
    if (!numberProperty(property, value)) return fallback;
    double max = 0.0;
    // Sliders author volume on a 0..max scale (commonly 0..100).
    if (numberValue(cJSON_GetObjectItemCaseSensitive(property, "max"), max) && max > 1.0) value /= max;
    return (float)std::clamp(value, 0.0, 1.0);
}

std::string lowered(const char* text) {
    std::string out = text ? text : "";
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

VideoFit classifyFit(const std::string& label) {
    if (label.find("fill") != std::string::npos || label.find("cover") != std::string::npos ||
        label.find("crop") != std::string::npos)
        return VideoFit::Fill;
    if (label.find("fit") != std::string::npos || label.find("contain") != std::string::npos) return VideoFit::Fit;
    return VideoFit::Default;
}

// A combo stores the selected option's value; its label carries the meaning.
VideoFit fitFrom(const cJSON* property) {
    if (!property) return VideoFit::Default;
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(property, "value");
    const cJSON* options = cJSON_GetObjectItemCaseSensitive(property, "options");
    const cJSON* option = nullptr;
    cJSON_ArrayForEach(option, options) {
        const cJSON* label = cJSON_GetObjectItemCaseSensitive(option, "label");
        if (cJSON_IsString(label) && cJSON_Compare(cJSON_GetObjectItemCaseSensitive(option, "value"), value, true))
            return classifyFit(lowered(label->valuestring));
    }
    return cJSON_IsString(value) ? classifyFit(lowered(value->valuestring)) : VideoFit::Default;
}
}  // namespace

VideoProperties parseVideoProperties(const cJSON* project_root) {
    VideoProperties out;
    const cJSON* general = cJSON_GetObjectItemCaseSensitive(project_root, "general");
    const cJSON* properties = cJSON_GetObjectItemCaseSensitive(general, "properties");
    if (!cJSON_IsObject(properties)) return out;

    double rate = 1.0;
    if (numberProperty(findProperty(properties, {"rate", "playbackrate", "playback_rate", "speed", "playbackspeed"}),
                       rate))
        out.rate = clampPlaybackRate((float)rate);
    out.volume = volumeFrom(findProperty(properties, {"volume", "audiovolume"}), out.volume);
    out.fit = fitFrom(findProperty(properties, {"scaling", "scalemode", "fit", "fitmode", "videofit"}));
    return out;
}
