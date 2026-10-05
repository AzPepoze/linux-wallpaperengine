#include "app/flag_config.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

char* readTextFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return nullptr;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return nullptr;
    }
    char* text = static_cast<char*>(malloc(static_cast<size_t>(size) + 1));
    if (!text) {
        fclose(file);
        return nullptr;
    }
    const size_t read = fread(text, 1, static_cast<size_t>(size), file);
    fclose(file);
    text[read] = '\0';
    return text;
}

cJSON* loadConfig() {
    const char* candidates[] = {"config.json", "../config.json", "../../config.json", "../../../config.json",
                                "../../../../config.json"};
    for (const char* path : candidates) {
        char* text = readTextFile(path);
        if (!text) continue;
        cJSON* json = cJSON_Parse(text);
        free(text);
        if (json) return json;
    }
    return nullptr;
}

}  // namespace

std::string flag_config::string(const char* key) {
    cJSON* config = loadConfig();
    if (!config) return "";
    std::string value;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (cJSON_IsString(item) && item->valuestring[0] != '\0') value = item->valuestring;
    cJSON_Delete(config);
    return value;
}

int flag_config::integer(const char* key) {
    cJSON* config = loadConfig();
    if (!config) return 0;
    int value = 0;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (cJSON_IsNumber(item)) value = static_cast<int>(item->valuedouble);
    cJSON_Delete(config);
    return value;
}

float flag_config::real(const char* key) {
    cJSON* config = loadConfig();
    if (!config) return 0.0f;
    float value = 0.0f;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (cJSON_IsNumber(item)) value = static_cast<float>(item->valuedouble);
    cJSON_Delete(config);
    return value;
}
