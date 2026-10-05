#include "app/package_extractor.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <vector>

#include "shared/assets/unpack.h"
#include "shared/core/build_config.h"
#include "shared/core/utils.h"
#include "shared/core/vfs.h"
#include "wallpaper/project_info.h"

namespace {
std::string packageFile(const WallpaperSource& source) {
    return source.is_pkg ? source.path : source.path + "/scene.pkg";
}

std::string extractOutputDir(const WallpaperSource& source, const CliOptions& opts) {
    if (opts.has_extract_dir) return opts.extract_dir;
    char wp_copy[1024] = {};
    strncpy(wp_copy, source.path.c_str(), sizeof(wp_copy) - 1);
    return std::string("extracted/") + basename(wp_copy);
}

std::string detectDefaultWallpaperPath() {
    std::vector<char> detected(1024, '\0');
    detect_default_wallpaper(detected.data(), detected.size());
    return std::string(detected.data());
}
}  // namespace

WallpaperSource resolveWallpaperSource(const CliOptions& opts) {
    WallpaperSource source;
    source.path = opts.wallpaper_arg;
    source.is_pkg = opts.pkg_flag;
    if (source.path.empty() && !opts.sandbox) source.path = detectDefaultWallpaperPath();
    if (!source.path.empty() && isPackageFile(source.path)) source.is_pkg = true;
    return source;
}

int runExtractOnly(const WallpaperSource& source, const CliOptions& opts) {
    const std::string out_dir = extractOutputDir(source, opts);
    mkdir("extracted", 0755);
    mkdir(out_dir.c_str(), 0755);

    const std::string pkg_file = packageFile(source);
    if (access(pkg_file.c_str(), F_OK) != 0) {
        fprintf(stderr, "extract-only: no scene.pkg found at %s\n", pkg_file.c_str());
        return EXIT_FAILURE;
    }
    return extract_pkg(pkg_file.c_str(), out_dir.c_str()) ? EXIT_SUCCESS : EXIT_FAILURE;
}

std::string prepareAssetRoot(const WallpaperSource& source) {
    const std::string pkg_file = packageFile(source);
    const bool has_package = source.is_pkg || access(pkg_file.c_str(), F_OK) == 0;
#if !DEBUG_BUILD
    // Scene packages are read in place from a memory map; other package kinds still need real files.
    if (has_package && vfs::mount(pkg_file.c_str())) {
        if (vfs::exists("pkg:/scene.json")) return vfs::kRoot;
        vfs::unmount();
    }
#endif
    mkdir("extracted", 0755);
    if (has_package) {
        const std::string wallpaper_id = std::filesystem::absolute(pkg_file).parent_path().filename().string();
        const std::filesystem::path out_dir = std::filesystem::path("extracted") / wallpaper_id;
        if (!extract_pkg(pkg_file.c_str(), out_dir.c_str())) return {};
        const std::filesystem::path project = std::filesystem::path(pkg_file).parent_path() / "project.json";
        std::error_code error;
        if (std::filesystem::exists(project, error)) {
            std::filesystem::copy_file(project, out_dir / "project.json",
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                fprintf(stderr, "Could not preserve project metadata: %s\n", error.message().c_str());
                return {};
            }
        } else {
            std::filesystem::remove(out_dir / "project.json", error);
        }
        return out_dir.string();
    }
    return source.path;
}
