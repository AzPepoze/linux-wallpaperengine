#include <cjson/cJSON.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "scene_parser.h"
#include "scene_parser_internal.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"

namespace wallpaper_engine {

using namespace scene_parser_detail;

namespace {

// Camera path files are named relative to the project root, which holds the scene file.
std::string siblingPath(const std::string& scene_json_path, const std::string& relative) {
    const size_t slash = scene_json_path.rfind('/');
    if (slash == std::string::npos) return relative;
    return scene_json_path.substr(0, slash + 1) + relative;
}

CameraTransformDocument parseTransform(const cJSON* transform) {
    CameraTransformDocument out;
    parseFloat(member(transform, "timestamp"), out.timestamp);
    parseVec(member(transform, "eye"), out.eye.data(), 3);
    parseVec(member(transform, "center"), out.center.data(), 3);
    parseVec(member(transform, "up"), out.up.data(), 3);
    return out;
}

CameraPathDocument parsePath(const cJSON* path) {
    CameraPathDocument out;
    readPlainString(path, "name", out.name);
    parseFloat(member(path, "duration"), out.duration);
    const cJSON* transform = nullptr;
    cJSON_ArrayForEach(transform, member(path, "transforms")) {
        if (cJSON_IsObject(transform)) out.transforms.push_back(parseTransform(transform));
    }
    return out;
}

void loadCameraPathFile(const std::string& file_path, std::vector<CameraPathDocument>& out) {
    char* json = read_file_to_string(file_path.c_str());
    if (!json) {
        LOG_W("Camera path file not found: %s", file_path.c_str());
        return;
    }
    if (!parseCameraPathJson(json, out)) LOG_W("Camera path file is invalid: %s", file_path.c_str());
    free(json);
}

}  // namespace

bool parseCameraPathJson(const char* json, std::vector<CameraPathDocument>& out) {
    cJSON* root = cJSON_Parse(json);
    const cJSON* paths = member(root, "paths");
    const bool valid = cJSON_IsArray(paths);
    if (valid) {
        const cJSON* path = nullptr;
        cJSON_ArrayForEach(path, paths) {
            if (cJSON_IsObject(path)) out.push_back(parsePath(path));
        }
    }
    cJSON_Delete(root);
    return valid;
}

namespace scene_parser_detail {

void parseCameraPathFiles(const cJSON* camera, std::vector<std::string>& out) {
    const cJSON* file = nullptr;
    cJSON_ArrayForEach(file, member(camera, "paths")) {
        if (cJSON_IsString(file) && file->valuestring) out.emplace_back(file->valuestring);
    }
}

void loadCameraPaths(const char* scene_json_path, SceneCameraDocument& camera) {
    for (const std::string& file : camera.path_files)
        loadCameraPathFile(siblingPath(scene_json_path, file), camera.paths);
}

}  // namespace scene_parser_detail
}  // namespace wallpaper_engine
