#ifndef PROJECT_INFO_H
#define PROJECT_INFO_H

#include <string>

#include "wallpaper/video/video_properties.h"

enum class ProjectType { None, Scene, Video, Web, Unsupported };

bool isVideoFile(const char* path);
bool isPackageFile(const std::string& path);

// What a wallpaper path (video file, scene directory, project.json directory
// or a directory holding a lone video) resolves to. Pure filesystem and JSON
// inspection; nothing here touches the GPU.
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
