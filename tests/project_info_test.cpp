#include "wallpaper/project_info.h"

#include <stdlib.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_util.h"
using test::expect;

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}

fs::path makeDir(const fs::path& base, const char* name) {
    fs::path dir = base / name;
    fs::create_directories(dir);
    return dir;
}

void testDirectVideoFile(const fs::path& base) {
    fs::path file = base / "clip.MP4";
    writeFile(file, "x");
    ProjectInfo info = ProjectInfo::detect(file.string());
    expect("direct video", info.type == ProjectType::Video, "type should be Video");
    expect("direct video", info.entry == file.string(), "entry should be the file");
    expect("direct video", ProjectInfo::detect((base / "missing.mp4").string()).type == ProjectType::None,
           "missing video file should not resolve");
}

void testSceneDirectory(const fs::path& base) {
    fs::path dir = makeDir(base, "scene_dir");
    writeFile(dir / "scene.json", "{}");
    writeFile(dir / "project.json", R"({"type":"scene","title":"Hello","file":"scene.json"})");
    ProjectInfo info = ProjectInfo::detect(dir.string());
    expect("scene dir", info.type == ProjectType::Scene, "type should be Scene");
    expect("scene dir", info.entry == dir.string() + "/scene.json", "entry should be scene.json");
    expect("scene dir", info.title == "Hello", "title should be parsed");
}

void testProjectJsonTypes(const fs::path& base) {
    fs::path video = makeDir(base, "video_project");
    writeFile(video / "movie.webm", "x");
    writeFile(video / "project.json", R"({"type":"Video","file":"movie.webm"})");
    ProjectInfo info = ProjectInfo::detect(video.string());
    expect("video project", info.type == ProjectType::Video, "type should be Video");
    expect("video project", info.entry == video.string() + "/movie.webm", "entry should follow project file");

    fs::path web = makeDir(base, "web_project");
    writeFile(web / "index.html", "<html></html>");
    writeFile(web / "project.json", R"({"type":"web","file":"index.html"})");
    info = ProjectInfo::detect(web.string());
    expect("web project", info.type == ProjectType::Web, "type should be Web");
    expect("web project", info.entry == web.string() + "/index.html", "entry should be the html file");

    fs::path app = makeDir(base, "app_project");
    writeFile(app / "project.json", R"({"type":"application","file":"run.exe"})");
    info = ProjectInfo::detect(app.string());
    expect("unsupported", info.type == ProjectType::Unsupported, "type should be Unsupported");
    expect("unsupported", info.type_name == "application", "type name should be kept");
}

void testVideoOnlyDirectory(const fs::path& base) {
    fs::path dir = makeDir(base, "video_only");
    writeFile(dir / "preview.gif", "x");
    writeFile(dir / "wall.mkv", "x");
    ProjectInfo info = ProjectInfo::detect(dir.string());
    expect("video only dir", info.type == ProjectType::Video, "type should be Video");
    expect("video only dir", info.entry == dir.string() + "/wall.mkv", "entry should be the video");
}

void testEmptyAndBroken(const fs::path& base) {
    fs::path empty = makeDir(base, "empty");
    expect("empty dir", ProjectInfo::detect(empty.string()).type == ProjectType::None, "empty dir has no wallpaper");
    expect("empty path", ProjectInfo::detect("").type == ProjectType::None, "empty path has no wallpaper");

    fs::path broken = makeDir(base, "broken");
    writeFile(broken / "project.json", "{not json");
    expect("broken json", ProjectInfo::detect(broken.string()).type == ProjectType::None, "broken json is ignored");

    fs::path dangling = makeDir(base, "dangling");
    writeFile(dangling / "project.json", R"({"type":"scene","file":"gone.json"})");
    expect("dangling file", ProjectInfo::detect(dangling.string()).type == ProjectType::None,
           "missing entry file does not resolve");
}

void testPresetDependency(const fs::path& base) {
    fs::path shared = makeDir(base, "shared_base");
    writeFile(shared / "scene.json", "{}");
    writeFile(shared / "project.json", R"({"type":"scene","title":"Base","file":"scene.json"})");

    fs::path preset = base / "shared_preset";
    fs::create_directories(preset);
    writeFile(preset / "project.json",
              R"({"dependency":"shared_base","preset":{"basecolor":"1 0 0"},"title":"Preset"})");
    ProjectInfo info = ProjectInfo::detect(preset.string());
    expect("preset", info.type == ProjectType::Scene, "preset should resolve to the base scene");
    expect("preset", info.root == shared.string(), "root should be the base folder");
    expect("preset", info.entry == shared.string() + "/scene.json", "entry should be the base scene");
    expect("preset", info.preset_root == preset.string(), "preset folder should be kept");
    expect("preset", contentRoot(preset.string()) == shared.string(), "content root should be the base folder");
    expect("preset", contentRoot(shared.string()) == shared.string(), "a normal project is its own content root");

    fs::path orphan = base / "orphan_preset";
    fs::create_directories(orphan);
    writeFile(orphan / "project.json", R"({"dependency":"not_installed","preset":{}})");
    expect("missing dependency", ProjectInfo::detect(orphan.string()).type == ProjectType::None,
           "a preset with a missing base does not resolve");
}

void testPackagePath() {
    expect("package", isPackageFile("a/b/scene.pkg"), ".pkg should be a package");
    expect("package", !isPackageFile("a/b/scene.json"), ".json is not a package");
    expect("package", !isPackageFile("pkg"), "short names are not packages");
}

}  // namespace

int main() {
    char pattern[] = "/tmp/lwe_project_test_XXXXXX";
    if (!mkdtemp(pattern)) {
        std::printf("FAIL: cannot create temp dir\n");
        return 1;
    }
    const fs::path base = pattern;

    testDirectVideoFile(base);
    testSceneDirectory(base);
    testProjectJsonTypes(base);
    testVideoOnlyDirectory(base);
    testEmptyAndBroken(base);
    testPresetDependency(base);
    testPackagePath();

    fs::remove_all(base);
    return test::finish("project info tests");
}
