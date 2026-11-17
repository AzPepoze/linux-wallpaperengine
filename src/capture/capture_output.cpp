#include "capture/capture_output.h"

#include <cjson/cJSON.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace capture {
namespace {

namespace fs = std::filesystem;

cJSON* shotJson(const ShotRecord& shot) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "index", shot.index);
    cJSON_AddNumberToObject(item, "requested_time", shot.requested_time);
    cJSON_AddNumberToObject(item, "scene_time", shot.scene_time);
    cJSON_AddStringToObject(item, "file", shot.file.c_str());
    return item;
}

cJSON* manifestTree(const ManifestInfo& info) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wallpaper", info.wallpaper.c_str());
    cJSON_AddNumberToObject(root, "width", info.width);
    cJSON_AddNumberToObject(root, "height", info.height);
    cJSON_AddNumberToObject(root, "fps", info.fps);
    cJSON* shots = cJSON_AddArrayToObject(root, "shots");
    for (const ShotRecord& shot : info.shots) {
        cJSON_AddItemToArray(shots, shotJson(shot));
    }
    return root;
}

}  // namespace

std::string shotFileName(int index, double scene_time) {
    std::ostringstream name;
    name << "shot-" << std::setw(4) << std::setfill('0') << index << "_t" << std::fixed << std::setprecision(3)
         << scene_time << "s.png";
    return name.str();
}

std::string manifestJson(const ManifestInfo& info) {
    cJSON* root = manifestTree(info);
    char* printed = cJSON_Print(root);
    std::string text = printed ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return text;
}

bool ensureDirectory(const std::string& dir) {
    std::error_code error;
    fs::create_directories(dir, error);
    return !error && fs::is_directory(dir, error);
}

bool writeManifest(const std::string& dir, const ManifestInfo& info) {
    if (!ensureDirectory(dir)) return false;
    std::ofstream file(fs::path(dir) / "manifest.json", std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << manifestJson(info);
    file.close();
    return !file.fail();
}

}  // namespace capture
