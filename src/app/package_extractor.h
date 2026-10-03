#ifndef PACKAGE_EXTRACTOR_H
#define PACKAGE_EXTRACTOR_H

#include <string>

#include "app/cli_options.h"

struct WallpaperSource {
    std::string path;
    bool is_pkg = false;
};

WallpaperSource resolveWallpaperSource(const CliOptions& opts);

// Runs the --extract-only mode and returns the process exit code.
int runExtractOnly(const WallpaperSource& source, const CliOptions& opts);

// Extracts a package if present and returns the directory to load assets from.
std::string prepareAssetRoot(const WallpaperSource& source);

#endif  // PACKAGE_EXTRACTOR_H
