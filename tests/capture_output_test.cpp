#include "capture/capture_output.h"

#include <cjson/cJSON.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "test_util.h"

namespace fs = std::filesystem;

int main() {
    using capture::ManifestInfo;
    using capture::ShotRecord;

    CHECK(capture::shotFileName(1, 1.0) == "shot-0001_t1.000s.png");
    CHECK(capture::shotFileName(12, 0.2) == "shot-0012_t0.200s.png");

    ManifestInfo info;
    info.wallpaper = "demo";
    info.width = 1280;
    info.height = 720;
    info.fps = 60;
    info.shots.push_back(ShotRecord{1, 1.0, 1.016, capture::shotFileName(1, 1.0)});
    info.shots.push_back(ShotRecord{2, 2.0, 2.0, capture::shotFileName(2, 2.0)});

    const std::string json = capture::manifestJson(info);
    CHECK(json.find("\"shots\"") != std::string::npos);
    cJSON* root = cJSON_Parse(json.c_str());
    CHECK(root != nullptr);
    if (root) {
        cJSON* shots = cJSON_GetObjectItemCaseSensitive(root, "shots");
        CHECK(cJSON_IsArray(shots));
        CHECK(cJSON_GetArraySize(shots) == 2);
        cJSON* wallpaper = cJSON_GetObjectItemCaseSensitive(root, "wallpaper");
        CHECK(cJSON_IsString(wallpaper) && std::string(wallpaper->valuestring) == "demo");
        cJSON_Delete(root);
    }

    const fs::path base = fs::temp_directory_path() / "capture_output_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);

    const fs::path dir = base / "x";
    CHECK(capture::ensureDirectory(dir.string()));
    CHECK(fs::is_directory(dir));

    const fs::path keep = dir / "keep.txt";
    {
        std::ofstream(keep) << "keep";
    }
    CHECK(capture::ensureDirectory(dir.string()));
    CHECK(fs::exists(keep));

    CHECK(capture::writeManifest(dir.string(), info));
    const fs::path manifest = dir / "manifest.json";
    CHECK(fs::exists(manifest));
    CHECK(fs::exists(keep));

    fs::remove_all(base, ignored);
    return test::finish("capture output");
}
