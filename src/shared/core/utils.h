#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

char* read_file_to_string(const char* path);
bool detect_engine_path(char* out_path, size_t max_len);
// Accepts either the Wallpaper Engine install root or its assets directory.
bool engine_path_from_assets_dir(const char* dir, char* out_path, size_t max_len);
void detect_default_wallpaper(char* out_path, size_t max_len);
// Reads `transition` and `transition_duration_ms` from the nearest config.json.
// Returns true when at least one key was present.
bool read_config_transition(char* out_effect, size_t effect_len, int* out_duration_ms);
// Reads `transition_mode` ("freeze"/"continue") from the nearest config.json.
bool read_config_transition_mode(char* out_mode, size_t mode_len);

#ifdef __cplusplus
}
#endif

#endif  // UTILS_H
