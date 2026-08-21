#include "effect_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "core/config.h"

namespace {
std::string normalizeUniformName(const std::string& name) {
    std::string normalized;
    size_t start = 0;
    if (name.size() > 2 && (name[0] == 'g' || name[0] == 'G' || name[0] == 'u' || name[0] == 'U') && name[1] == '_')
        start = 2;
    for (size_t i = start; i < name.size(); ++i) {
        const unsigned char character = static_cast<unsigned char>(name[i]);
        if (std::isalnum(character)) normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

void readRenderTargetBindings(const cJSON* pass_config, EffectPassConfig& config) {
    const cJSON* target = cJSON_GetObjectItemCaseSensitive(pass_config, "target");
    if (cJSON_IsString(target) && target->valuestring) config.render_target_name = target->valuestring;
    const cJSON* bindings = cJSON_GetObjectItemCaseSensitive(pass_config, "bind");
    if (!cJSON_IsArray(bindings)) return;
    cJSON* binding;
    cJSON_ArrayForEach(binding, bindings) {
        const cJSON* slot = cJSON_GetObjectItemCaseSensitive(binding, "index");
        const cJSON* source = cJSON_GetObjectItemCaseSensitive(binding, "name");
        if (cJSON_IsNumber(slot) && cJSON_IsString(source) && source->valuestring)
            config.texture_bindings[slot->valueint] = source->valuestring;
    }
}
}  // namespace

void EffectParser::mergeObject(cJSON*& target, const cJSON* source) {
    if (!cJSON_IsObject(source)) return;
    if (!target) target = cJSON_CreateObject();
    cJSON* item;
    cJSON_ArrayForEach(item, source) {
        if (!item->string) continue;
        cJSON_DeleteItemFromObjectCaseSensitive(target, item->string);
        cJSON_AddItemToObject(target, item->string, cJSON_Duplicate(item, 1));
    }
}

bool EffectParser::readFloats(const cJSON* node, std::vector<float>& out) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        out.push_back((float)node->valuedouble);
        return true;
    }
    if (cJSON_IsBool(node)) {
        out.push_back(cJSON_IsTrue(node) ? 1.0f : 0.0f);
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        std::string text = node->valuestring;
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream stream(text);
        float value = 0.0f;
        bool parsed = false;
        while (stream >> value) {
            out.push_back(value);
            parsed = true;
        }
        return parsed;
    }
    if (cJSON_IsArray(node)) {
        bool parsed = false;
        cJSON* item;
        cJSON_ArrayForEach(item, node) {
            parsed = readFloats(item, out) || parsed;
        }
        return parsed;
    }
    return cJSON_IsObject(node) && readFloats(cJSON_GetObjectItemCaseSensitive(node, "value"), out);
}

bool EffectParser::readInt(const cJSON* node, int& value) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        value = node->valueint;
        return true;
    }
    if (cJSON_IsBool(node)) {
        value = cJSON_IsTrue(node) ? 1 : 0;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        char* end = nullptr;
        const long parsed = strtol(node->valuestring, &end, 10);
        if (!end || end == node->valuestring) return false;
        value = (int)parsed;
        return true;
    }
    if (cJSON_IsArray(node) && cJSON_GetArraySize(node) > 0) return readInt(cJSON_GetArrayItem(node, 0), value);
    return cJSON_IsObject(node) && readInt(cJSON_GetObjectItemCaseSensitive(node, "value"), value);
}

void EffectParser::readCombos(const cJSON* config, std::map<std::string, int>& combos) {
    const cJSON* combo_node = config ? cJSON_GetObjectItemCaseSensitive(config, "combos") : nullptr;
    if (!cJSON_IsObject(combo_node)) return;
    cJSON* item;
    cJSON_ArrayForEach(item, combo_node) {
        int value = 0;
        if (item->string && readInt(item, value)) combos[item->string] = value;
    }
}

void EffectParser::readValuesObject(const cJSON* values, std::map<std::string, std::vector<float>>& uniforms) {
    if (!cJSON_IsObject(values)) return;
    cJSON* item;
    cJSON_ArrayForEach(item, values) {
        std::vector<float> parsed;
        if (item->string && readFloats(item, parsed) && !parsed.empty()) uniforms[item->string] = std::move(parsed);
    }
}

void EffectParser::readUniformValues(const cJSON* config, std::map<std::string, std::vector<float>>& uniforms) {
    readValuesObject(config ? cJSON_GetObjectItemCaseSensitive(config, "constantshadervalues") : nullptr, uniforms);
}

EffectPassConfig EffectParser::buildPassConfig(const cJSON* material_config, const cJSON* pass_config,
                                               const cJSON* instance_config, int pass_index) {
    EffectPassConfig config;
    config.pass_index = pass_index;
    const cJSON* material = material_config ? material_config : pass_config;
    const cJSON* shader = cJSON_GetObjectItemCaseSensitive(material, "shader");
    if (cJSON_IsString(shader) && shader->valuestring) config.shader_path = shader->valuestring;
    const cJSON* material_reference = pass_config ? cJSON_GetObjectItemCaseSensitive(pass_config, "material") : nullptr;
    if (cJSON_IsString(material_reference) && material_reference->valuestring)
        config.material_reference = material_reference->valuestring;

    readUniformValues(material, config.material_uniform_values);
    readCombos(material, config.material_combos);
    if (material != pass_config) {
        readUniformValues(pass_config, config.pass_uniform_values);
        readCombos(pass_config, config.pass_combos);
    }
    readUniformValues(instance_config, config.instance_uniform_values);
    readCombos(instance_config, config.instance_combos);

    config.uniform_values = config.material_uniform_values;
    config.uniform_values.insert(config.pass_uniform_values.begin(), config.pass_uniform_values.end());
    for (const auto& [name, values] : config.pass_uniform_values) config.uniform_values[name] = values;
    for (const auto& [name, values] : config.instance_uniform_values) config.uniform_values[name] = values;
    config.combos = config.material_combos;
    for (const auto& [name, value] : config.pass_combos) config.combos[name] = value;
    for (const auto& [name, value] : config.instance_combos) config.combos[name] = value;

    readRenderTargetBindings(pass_config, config);
    const cJSON* enabled = instance_config ? cJSON_GetObjectItemCaseSensitive(instance_config, "enabled") : nullptr;
    if (cJSON_IsBool(enabled)) config.enabled = cJSON_IsTrue(enabled);
    return config;
}

EffectConfig EffectParser::parse(const cJSON* effect_config, const cJSON* instance_config) {
    EffectConfig config;
    const cJSON* target_definitions = cJSON_GetObjectItemCaseSensitive(effect_config, "fbos");
    std::map<std::string, float> target_scales;
    if (cJSON_IsArray(target_definitions)) {
        cJSON* target;
        cJSON_ArrayForEach(target, target_definitions) {
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(target, "name");
            const cJSON* scale = cJSON_GetObjectItemCaseSensitive(target, "scale");
            if (cJSON_IsString(name) && name->valuestring && cJSON_IsNumber(scale) && scale->valuedouble > 0.0)
                target_scales[name->valuestring] = static_cast<float>(scale->valuedouble);
        }
    }
    const cJSON* passes = cJSON_GetObjectItemCaseSensitive(effect_config, "passes");
    const cJSON* instance_passes =
        instance_config ? cJSON_GetObjectItemCaseSensitive(instance_config, "passes") : nullptr;
    if (!cJSON_IsArray(passes)) return config;
    cJSON* pass;
    int pass_index = 0;
    cJSON_ArrayForEach(pass, passes) {
        const cJSON* instance_pass =
            cJSON_IsArray(instance_passes) ? cJSON_GetArrayItem(instance_passes, pass_index) : nullptr;
        EffectPassConfig pass_config = buildPassConfig(nullptr, pass, instance_pass, pass_index++);
        const auto scale = target_scales.find(pass_config.render_target_name);
        if (scale != target_scales.end()) pass_config.render_target_scale = scale->second;
        config.passes.push_back(std::move(pass_config));
    }
    return config;
}

std::vector<ShaderUniformConfig> EffectParser::extractShaderUniforms(const std::string& source) {
    std::vector<ShaderUniformConfig> result;
    size_t search = 0;
    while ((search = source.find("uniform", search)) != std::string::npos) {
        if (search > 0 && (std::isalnum(static_cast<unsigned char>(source[search - 1])) || source[search - 1] == '_')) {
            search += 7;
            continue;
        }
        const size_t semicolon = source.find(';', search + 7);
        if (semicolon == std::string::npos) break;
        std::istringstream stream(source.substr(search + 7, semicolon - search - 7));
        std::vector<std::string> tokens;
        std::string token;
        bool is_sampler = false;
        while (stream >> token) {
            is_sampler = is_sampler || token.find("sampler") != std::string::npos;
            tokens.push_back(token);
        }
        if (!is_sampler && tokens.size() >= 2) {
            ShaderUniformConfig config;
            config.type = tokens.front();
            config.name = tokens.back();
            const size_t array = config.name.find('[');
            if (array != std::string::npos) config.name.erase(array);
            const size_t assignment = config.name.find('=');
            if (assignment != std::string::npos) config.name.erase(assignment);
            const size_t line_end = source.find('\n', semicolon + 1);
            const size_t comment = source.find("//", semicolon + 1);
            if (comment != std::string::npos && (line_end == std::string::npos || comment < line_end)) {
                const size_t json_start = source.find('{', comment + 2);
                const size_t json_end =
                    line_end == std::string::npos ? source.rfind('}') : source.rfind('}', line_end - 1);
                if (json_start != std::string::npos && json_end != std::string::npos && json_end >= json_start) {
                    cJSON* metadata = cJSON_Parse(source.substr(json_start, json_end - json_start + 1).c_str());
                    if (metadata) {
                        const cJSON* material = cJSON_GetObjectItemCaseSensitive(metadata, "material");
                        const cJSON* type = cJSON_GetObjectItemCaseSensitive(metadata, "type");
                        const cJSON* defaults = cJSON_GetObjectItemCaseSensitive(metadata, "default");
                        if (cJSON_IsString(material) && material->valuestring)
                            config.material_name = material->valuestring;
                        if (cJSON_IsString(type) && type->valuestring) config.type = type->valuestring;
                        config.has_default =
                            readFloats(defaults, config.default_values) && !config.default_values.empty();
                        cJSON_Delete(metadata);
                    }
                }
            }
            if (!config.name.empty()) result.push_back(std::move(config));
        }
        search = semicolon + 1;
    }
    return result;
}

bool EffectParser::resolveUniformName(const std::string& authored_name,
                                      const std::vector<ShaderUniformConfig>& shader_uniforms,
                                      std::string& resolved_name) {
    std::string preferred_name = authored_name;
    const auto mapped_name = Config::kUniformNameMap.find(authored_name);
    if (mapped_name != Config::kUniformNameMap.end()) preferred_name = mapped_name->second;
    const auto find = [&](const std::string& requested) {
        for (const auto& uniform : shader_uniforms) {
            if (uniform.name == requested) {
                resolved_name = uniform.name;
                return true;
            }
        }
        return false;
    };
    if (find(preferred_name) || (preferred_name != authored_name && find(authored_name))) return true;
    for (const auto& uniform : shader_uniforms) {
        if (!uniform.material_name.empty() &&
            normalizeUniformName(uniform.material_name) == normalizeUniformName(authored_name)) {
            resolved_name = uniform.name;
            return true;
        }
    }
    for (const auto& uniform : shader_uniforms) {
        if (normalizeUniformName(uniform.name) == normalizeUniformName(preferred_name)) {
            resolved_name = uniform.name;
            return true;
        }
    }
    return false;
}
