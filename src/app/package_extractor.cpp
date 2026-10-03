#include "app/package_extractor.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shared/assets/unpack.h"
#include "shared/core/utils.h"

namespace {
bool hasPkgExtension(const std::string& path) {
    return path.size() >= 4 && path.compare(path.size() - 4, 4, ".pkg") == 0;
}

std::string packageFile(const WallpaperSource& source) {
    return source.is_pkg ? source.path : source.path + "/scene.pkg";
}

std::string extractOutputDir(const WallpaperSource& source, const CliOptions& opts) {
    if (opts.has_extract_dir) return opts.extract_dir;
    char wp_copy[1024] = {};
    strncpy(wp_copy, source.path.c_str(), sizeof(wp_copy) - 1);
    return std::string("extracted/") + basename(wp_copy);
}
}  // namespace

WallpaperSource resolveWallpaperSource(const CliOptions& opts) {
    WallpaperSource source;
    source.path = opts.wallpaper_arg;
    source.is_pkg = opts.pkg_flag;
    if (source.path.empty() && !opts.sandbox) {
        char detected[1024] = {};
        detect_default_wallpaper(detected, sizeof(detected));
        source.path = detected;
    }
    if (!source.path.empty() && hasPkgExtension(source.path)) source.is_pkg = true;
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
    mkdir("extracted", 0755);
    const std::string pkg_file = packageFile(source);
    if (source.is_pkg || access(pkg_file.c_str(), F_OK) == 0) {
        extract_pkg(pkg_file.c_str(), "extracted");
        return "extracted";
    }
    return source.path;
}
