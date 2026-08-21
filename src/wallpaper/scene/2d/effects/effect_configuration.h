#ifndef EFFECT_CONFIGURATION_H
#define EFFECT_CONFIGURATION_H

#include <cjson/cJSON.h>

#include <map>
#include <string>
#include <vector>

// Owns the Wallpaper Engine JSON value conventions used by effect passes.
// This is deliberately effect-specific: it is not a generic JSON wrapper.
class EffectConfiguration {
   public:
    static void mergeObject(cJSON*& target, const cJSON* source);
    static bool readFloats(const cJSON* node, std::vector<float>& out);
    static bool readInt(const cJSON* node, int& value);
    static void readCombos(const cJSON* config, std::map<std::string, int>& combos);
    static void readValuesObject(const cJSON* values, std::map<std::string, std::vector<float>>& uniforms);
    static void readUniformValues(const cJSON* config, std::map<std::string, std::vector<float>>& uniforms);
};

#endif  // EFFECT_CONFIGURATION_H
