#ifndef PROJECT_INFO_H
#define PROJECT_INFO_H

#include <string>

#include "wallpaper/video/video_properties.h"

enum class ProjectType { None, Scene, Video, Web, Unsupported };

bool isVideoFile(const char* path);
bool isPackageFile(const std::string& path);

// The base wallpaper folder a preset project depends on; empty when `path` is not a preset.
std::string presetBaseRoot(const std::string& path);
// The folder that holds scene.pkg: the base wallpaper for a preset, otherwise `path`.
std::string contentRoot(const std::string& path);

// Resolves a wallpaper path by filesystem and JSON inspection only; never touches the GPU.
struct ProjectInfo {
    ProjectType type = ProjectType::None;
    std::string root;
    std::string entry;
    std::string title;
    std::string type_name;
    // The preset folder whose values apply to this base wallpaper; empty for a normal project.
    std::string preset_root;
    VideoProperties video;

    static ProjectInfo detect(const std::string& path);
};

#endif  // PROJECT_INFO_H
