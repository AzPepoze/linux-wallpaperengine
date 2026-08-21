#ifndef EFFECT_PARSER_H
#define EFFECT_PARSER_H

#include <cjson/cJSON.h>

#include <map>
#include <string>
#include <vector>

struct ShaderUniformConfig {
    std::string name;
    std::string material_name;
    std::string type = "float";
    std::vector<float> default_values;
    bool has_default = false;
};

struct EffectPassConfig {
    std::string shader_path;
    std::string material_reference;
    std::map<std::string, std::vector<float>> uniform_values;
    std::map<std::string, int> combos;
    std::map<std::string, std::vector<float>> material_uniform_values;
    std::map<std::string, std::vector<float>> pass_uniform_values;
    std::map<std::string, std::vector<float>> instance_uniform_values;
    std::map<std::string, int> material_combos;
    std::map<std::string, int> pass_combos;
    std::map<std::string, int> instance_combos;
    std::string render_target_name;
    float render_target_scale = 1.0f;
    std::map<int, std::string> texture_bindings;
    bool enabled = true;
    int pass_index = 0;
};

struct EffectConfig {
    std::vector<EffectPassConfig> passes;
};

class EffectParser {
   public:
    static void mergeObject(cJSON*& target, const cJSON* source);
    static bool readFloats(const cJSON* node, std::vector<float>& out);
    static bool readInt(const cJSON* node, int& value);
    static void readCombos(const cJSON* config, std::map<std::string, int>& combos);
    static void readValuesObject(const cJSON* values, std::map<std::string, std::vector<float>>& uniforms);
    static void readUniformValues(const cJSON* config, std::map<std::string, std::vector<float>>& uniforms);

    static EffectPassConfig buildPassConfig(const cJSON* material_config, const cJSON* pass_config,
                                            const cJSON* instance_config, int pass_index = 0);
    static EffectConfig parse(const cJSON* effect_config, const cJSON* instance_config = nullptr);
    static std::vector<ShaderUniformConfig> extractShaderUniforms(const std::string& source);
    static bool resolveUniformName(const std::string& authored_name,
                                   const std::vector<ShaderUniformConfig>& shader_uniforms, std::string& resolved_name);
};

#endif  // EFFECT_PARSER_H
