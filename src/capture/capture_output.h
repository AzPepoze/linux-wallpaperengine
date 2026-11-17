#ifndef CAPTURE_OUTPUT_H
#define CAPTURE_OUTPUT_H

#include <string>
#include <vector>

namespace capture {

struct ShotRecord {
    int index = 0;
    double requested_time = 0;
    double scene_time = 0;
    std::string file;
};

struct ManifestInfo {
    std::string wallpaper;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::vector<ShotRecord> shots;
};

// Returns e.g. "shot-0001_t1.000s.png".
std::string shotFileName(int index, double scene_time);
std::string manifestJson(const ManifestInfo& info);
// Creates the directory (and parents) if needed. Never deletes anything.
bool ensureDirectory(const std::string& dir);
// Writes dir/manifest.json, creating dir first.
bool writeManifest(const std::string& dir, const ManifestInfo& info);

}  // namespace capture

#endif  // CAPTURE_OUTPUT_H
