#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "config.h"
#include "logger.h"

char* read_file_to_string(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(size + 1);
    if (buf) {
        size_t n = fread(buf, 1, size, f);
        buf[n] = '\0';
    }
    fclose(f);
    return buf;
}

void detect_engine_path(char* out_path, size_t max_len) {
    const char* env_path = getenv("WALLPAPER_ENGINE_PATH");
    if (env_path && access(env_path, F_OK) == 0) {
        strncpy(out_path, env_path, max_len - 1);
        LOG_I("Found Wallpaper Engine at: %s (from WALLPAPER_ENGINE_PATH)", out_path);
        return;
    }

    const char* home = getenv("HOME");
    std::string home_str = home ? home : "";

    for (const auto& path : Config::kEngineSearchPaths) {
        std::string full_path = path;
        if (full_path.find("~/") == 0 && !home_str.empty()) {
            full_path.replace(0, 1, home_str);
        }
        if (access(full_path.c_str(), F_OK) == 0) {
            strncpy(out_path, full_path.c_str(), max_len - 1);
            LOG_I("Found Wallpaper Engine at: %s", out_path);
            return;
        }
    }
    LOG_W("Wallpaper Engine assets folder not found!");
}
