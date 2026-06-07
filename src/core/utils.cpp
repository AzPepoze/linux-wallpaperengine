#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    const char* paths[] = {"/mnt/6AB2DBF3B2DBC1AD/Program Files (x86)/Steam/steamapps/common/wallpaper_engine",
                           "/home/azpepoze/.local/share/Steam/steamapps/common/wallpaper_engine",
                           "/home/azpepoze/.steam/steam/steamapps/common/wallpaper_engine"};
    for (int i = 0; i < 3; i++) {
        if (access(paths[i], F_OK) == 0) {
            strncpy(out_path, paths[i], max_len - 1);
            LOG_I("Found Wallpaper Engine at: %s", out_path);
            return;
        }
    }
    LOG_W("Wallpaper Engine assets folder not found!");
}
