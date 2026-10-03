#include "wallpaper/project_info.h"

#include <cjson/cJSON.h>
#include <dirent.h>
#include <strings.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace {
bool exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

const char* jsonString(cJSON* root, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (cJSON_IsString(item) && item->valuestring && item->valuestring[0] != '\0') ? item->valuestring : nullptr;
}

std::string findVideoInDirectory(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) return "";
    std::string found;
    while (struct dirent* entry = readdir(dir)) {
        if (isVideoFile(entry->d_name)) {
            found = directory + "/" + entry->d_name;
            break;
        }
    }
    closedir(dir);
    return found;
}

// Returns true when project.json alone decides the outcome.
bool applyProjectJson(const std::string& directory, ProjectInfo& info) {
    const std::string json_text = readFile(directory + "/project.json");
    cJSON* root = json_text.empty() ? nullptr : cJSON_Parse(json_text.c_str());
    if (!root) return false;

    if (const char* title = jsonString(root, "title")) info.title = title;
    info.video = parseVideoProperties(root);
    const char* type = jsonString(root, "type");
    const bool is_web = type && strcasecmp(type, "web") == 0;
    bool decided = false;
    if (type && !is_web && strcasecmp(type, "scene") != 0 && strcasecmp(type, "video") != 0) {
        info.type = ProjectType::Unsupported;
        info.type_name = type;
        decided = true;
    } else if (const char* file = jsonString(root, "file")) {
        const std::string target = directory + "/" + file;
        if (exists(target)) {
            info.root = directory;
            info.entry = target;
            info.type = is_web                        ? ProjectType::Web
                        : isVideoFile(target.c_str()) ? ProjectType::Video
                                                      : ProjectType::Scene;
            decided = true;
        }
    }
    cJSON_Delete(root);
    return decided;
}
}  // namespace

bool isVideoFile(const char* path) {
    if (!path) return false;
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".webm") == 0 || strcasecmp(ext, ".mkv") == 0 ||
            strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".wmv") == 0);
}

bool isPackageFile(const std::string& path) {
    return path.size() >= 4 && path.compare(path.size() - 4, 4, ".pkg") == 0;
}

ProjectInfo ProjectInfo::detect(const std::string& path) {
    ProjectInfo info;
    info.root = path;
    if (path.empty()) return info;

    if (isVideoFile(path.c_str()) && exists(path)) {
        info.type = ProjectType::Video;
        info.entry = path;
        return info;
    }

    const std::string scene_path = path + "/scene.json";
    const bool has_scene = exists(scene_path);
    if (exists(path + "/project.json")) {
        ProjectInfo from_project = info;
        const bool decided = applyProjectJson(path, from_project);
        if (!has_scene && decided) return from_project;
        info.title = from_project.title;
        info.video = from_project.video;
    }
    if (has_scene) {
        info.type = ProjectType::Scene;
        info.entry = scene_path;
        return info;
    }

    const std::string video = findVideoInDirectory(path);
    if (!video.empty()) {
        info.type = ProjectType::Video;
        info.entry = video;
    }
    return info;
}
