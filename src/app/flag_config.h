#ifndef FLAG_CONFIG_H
#define FLAG_CONFIG_H

#include <string>

// Fallback values from the nearest config.json, read for keys the CLI did not set.
namespace flag_config {

std::string loadedPath();             // path of the first successfully parsed config, or empty
std::string string(const char* key);  // "" when the key is absent
int integer(const char* key);         // 0 when the key is absent
float real(const char* key);          // 0 when the key is absent

}  // namespace flag_config

#endif  // FLAG_CONFIG_H
