#include "utils.h"

#include <cjson/cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "config.h"
#include "logger.h"

namespace {
bool hasEngineAssets(const char* path) {
    if (!path || !path[0]) return false;
    const std::string assets_dir = std::string(path) + "/assets";
    return access(assets_dir.c_str(), F_OK) == 0;
}

void copyPath(char* out_path, size_t max_len, const char* path) {
    if (!out_path || max_len == 0 || !path) return;
    strncpy(out_path, path, max_len - 1);
    out_path[max_len - 1] = '\0';
}
}  // namespace

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

bool detect_engine_path(char* out_path, size_t max_len) {
    const char* env_path = getenv("WALLPAPER_ENGINE_PATH");
    if (hasEngineAssets(env_path)) {
        copyPath(out_path, max_len, env_path);
        LOG_I("Found Wallpaper Engine at: %s (from WALLPAPER_ENGINE_PATH)", out_path);
        return true;
    }

    auto try_config = [&](const char* config_file) -> bool {
        char* config_str = read_file_to_string(config_file);
        if (!config_str) return false;
        cJSON* config_json = cJSON_Parse(config_str);
        if (config_json) {
            cJSON* path = cJSON_GetObjectItemCaseSensitive(config_json, "engine_path");
            if (cJSON_IsString(path) && hasEngineAssets(path->valuestring)) {
                copyPath(out_path, max_len, path->valuestring);
                LOG_I("Found Wallpaper Engine at: %s (from %s)", out_path, config_file);
                cJSON_Delete(config_json);
                free(config_str);
                return true;
            }
            cJSON_Delete(config_json);
        }
        free(config_str);
        return false;
    };

    const char* config_candidates[] = {"config.json", "../config.json", "../../config.json", "../../../config.json",
                                       "../../../../config.json"};
    for (const char* cfg : config_candidates) {
        if (try_config(cfg)) return true;
    }

    const char* home = getenv("HOME");
    std::string home_str = home ? home : "";

    for (const auto& path : Config::kEngineSearchPaths) {
        std::string full_path = path;
        if (full_path.find("~/") == 0 && !home_str.empty()) {
            full_path.replace(0, 1, home_str);
        }
        if (hasEngineAssets(full_path.c_str())) {
            copyPath(out_path, max_len, full_path.c_str());
            LOG_I("Found Wallpaper Engine at: %s", out_path);
            return true;
        }
    }

    LOG_E("Wallpaper Engine assets folder not found!");
    if (out_path && max_len > 0) out_path[0] = '\0';
    return false;
}

void detect_default_wallpaper(char* out_path, size_t max_len) {
    if (!out_path || max_len == 0) return;

    const char* env_keys[] = {"DEFAULT_WALLPAPER_PATH", "WALLPAPER_PATH"};
    for (const char* env_key : env_keys) {
        const char* env_val = getenv(env_key);
        if (env_val && env_val[0] != '\0' && access(env_val, F_OK) == 0) {
            copyPath(out_path, max_len, env_val);
            LOG_I("Found default wallpaper at: %s (from %s)", out_path, env_key);
            return;
        }
    }

    const char* config_candidates[] = {"config.json", "../config.json", "../../config.json", "../../../config.json",
                                       "../../../../config.json"};
    for (const char* cfg : config_candidates) {
        char* config_str = read_file_to_string(cfg);
        if (config_str) {
            cJSON* config_json = cJSON_Parse(config_str);
            if (config_json) {
                cJSON* path = cJSON_GetObjectItemCaseSensitive(config_json, "default_wallpaper");
                if (!path) {
                    path = cJSON_GetObjectItemCaseSensitive(config_json, "wallpaper_path");
                }
                if (cJSON_IsString(path) && path->valuestring[0] != '\0' && access(path->valuestring, F_OK) == 0) {
                    copyPath(out_path, max_len, path->valuestring);
                    LOG_I("Found default wallpaper at: %s (from %s)", out_path, cfg);
                    cJSON_Delete(config_json);
                    free(config_str);
                    return;
                }
                cJSON_Delete(config_json);
            }
            free(config_str);
        }
    }
}
