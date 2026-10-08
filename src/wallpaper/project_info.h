#ifndef PROJECT_INFO_H
#define PROJECT_INFO_H

#include <string>

#include "wallpaper/video/video_properties.h"

enum class ProjectType { None, Scene, Video, Web, Unsupported };

bool isVideoFile(const char* path);
bool isPackageFile(const std::string& path);

// Resolves a wallpaper path by filesystem and JSON inspection only; never touches the GPU.
struct ProjectInfo {
    ProjectType type = ProjectType::None;
    std::string root;
    std::string entry;
    std::string title;
    std::string type_name;
    VideoProperties video;

    static ProjectInfo detect(const std::string& path);
};

#endif  // PROJECT_INFO_H
