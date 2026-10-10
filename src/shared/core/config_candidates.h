#ifndef CONFIG_CANDIDATES_H
#define CONFIG_CANDIDATES_H

#include <string>
#include <vector>

// Set by --config; when non-empty it replaces the search below.
inline std::string& configPathOverride() {
    static std::string path;
    return path;
}

// The config.json to read: the --config file alone, else the working directory and its parents.
inline std::vector<std::string> configCandidates() {
    if (!configPathOverride().empty()) return {configPathOverride()};
    return {"config.json", "../config.json", "../../config.json", "../../../config.json", "../../../../config.json"};
}

#endif  // CONFIG_CANDIDATES_H
