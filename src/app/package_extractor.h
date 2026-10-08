#ifndef PACKAGE_EXTRACTOR_H
#define PACKAGE_EXTRACTOR_H

#include <string>

#include "app/cli_options.h"

struct WallpaperSource {
    std::string path;
    bool is_pkg = false;
};

WallpaperSource resolveWallpaperSource(const CliOptions& opts);

int runExtractOnly(const WallpaperSource& source, const CliOptions& opts);

std::string prepareAssetRoot(const WallpaperSource& source);

#endif  // PACKAGE_EXTRACTOR_H
