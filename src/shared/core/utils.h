#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

char* read_file_to_string(const char* path);
bool detect_engine_path(char* out_path, size_t max_len);
void detect_default_wallpaper(char* out_path, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif  // UTILS_H
