#ifndef VIDEO_PROPERTIES_H
#define VIDEO_PROPERTIES_H

#include <cjson/cJSON.h>

enum class VideoFit { Default, Fit, Fill };

// Defaults of the playback-related entries in project.json general.properties.
struct VideoProperties {
    float rate = 1.0f;
    float volume = 1.0f;
    VideoFit fit = VideoFit::Default;
};

VideoProperties parseVideoProperties(const cJSON* project_root);

#endif  // VIDEO_PROPERTIES_H
