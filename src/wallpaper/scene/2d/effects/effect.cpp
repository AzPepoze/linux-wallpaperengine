#include "effect.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/config.h"
#include "core/context.h"
#include "core/engine_context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "formats/wallpaper_engine/scene/scene_document.h"
#include "render/shader/shader_processor.h"
#include "sokol_app.h"

namespace {
void mergeJsonObject(cJSON*& target, cJSON* source) {
    if (!cJSON_IsObject(source)) return;
    if (!target) target = cJSON_CreateObject();

    cJSON* item;
    cJSON_ArrayForEach(item, source) {
        if (!item->string) continue;
        cJSON_DeleteItemFromObjectCaseSensitive(target, item->string);
        cJSON_AddItemToObject(target, item->string, cJSON_Duplicate(item, 1));
    }
}

bool jsonToFloats(cJSON* node, std::vector<float>& out) {
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
            parsed = jsonToFloats(item, out) || parsed;
        }
        return parsed;
    }
    if (cJSON_IsObject(node)) {
        // Wallpaper Engine wraps animated/user-bound values in objects while
        // preserving the authored fallback in `value`.
        return jsonToFloats(cJSON_GetObjectItemCaseSensitive(node, "value"), out);
    }
    return false;
}

bool jsonToInt(cJSON* node, int& value) {
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
        long parsed = strtol(node->valuestring, &end, 10);
        if (end && end != node->valuestring) {
            value = (int)parsed;
            return true;
        }
        return false;
    }
    if (cJSON_IsArray(node) && cJSON_GetArraySize(node) > 0) {
        return jsonToInt(cJSON_GetArrayItem(node, 0), value);
    }
    if (cJSON_IsObject(node)) {
        return jsonToInt(cJSON_GetObjectItemCaseSensitive(node, "value"), value);
    }
    return false;
}

void readCombos(cJSON* config, std::map<std::string, int>& combos) {
    if (!config) return;
    cJSON* combo_node = cJSON_GetObjectItemCaseSensitive(config, "combos");
    if (!cJSON_IsObject(combo_node)) return;

    cJSON* item;
    cJSON_ArrayForEach(item, combo_node) {
        if (!item->string) continue;
        int value = 0;
        if (jsonToInt(item, value)) combos[item->string] = value;
    }
}

std::string normalizeUniformName(const std::string& name) {
    std::string normalized;
    size_t start = 0;
    if (name.size() > 2 && (name[0] == 'g' || name[0] == 'G') && name[1] == '_') start = 2;
    for (size_t i = start; i < name.size(); ++i) {
        unsigned char c = (unsigned char)name[i];
        if (std::isalnum(c)) normalized.push_back((char)std::tolower(c));
    }
    return normalized;
}

std::vector<std::string> extractUniformNames(const std::string& source) {
    std::vector<std::string> result;
    size_t search = 0;
    while ((search = source.find("uniform", search)) != std::string::npos) {
        if (search > 0) {
            unsigned char before = (unsigned char)source[search - 1];
            if (std::isalnum(before) || before == '_') {
                search += 7;
                continue;
            }
        }

        const size_t semicolon = source.find(';', search + 7);
        if (semicolon == std::string::npos) break;
        std::string declaration = source.substr(search + 7, semicolon - search - 7);
        const size_t comment = declaration.find("//");
        if (comment != std::string::npos) declaration.erase(comment);

        std::istringstream stream(declaration);
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) tokens.push_back(token);
        if (tokens.size() >= 2) {
            std::string name = tokens.back();
            const size_t array = name.find('[');
            if (array != std::string::npos) name.erase(array);
            const size_t assign = name.find('=');
            if (assign != std::string::npos) name.erase(assign);
            if (!name.empty()) result.push_back(name);
        }
        search = semicolon + 1;
    }
    return result;
}

std::string resolveUniformName(const std::string& authored, const std::vector<std::string>& shader_uniforms) {
    std::string preferred = authored;
    auto mapped = Config::kUniformNameMap.find(authored);
    if (mapped != Config::kUniformNameMap.end()) preferred = mapped->second;

    for (const auto& candidate : shader_uniforms) {
        if (candidate == preferred) return candidate;
    }

    const std::string wanted = normalizeUniformName(preferred);
    for (const auto& candidate : shader_uniforms) {
        if (normalizeUniformName(candidate) == wanted) return candidate;
    }

    // Most WE constant keys omit the g_ prefix and use lowercase spelling.
    // Keep the old fallback for custom shaders where declaration discovery
    // cannot identify a matching name.
    if (preferred.find("g_") != 0 && !preferred.empty()) {
        std::string fallback = "g_";
        fallback += (char)std::toupper((unsigned char)preferred[0]);
        fallback += preferred.substr(1);
        return fallback;
    }
    return preferred;
}

void setComboDefine(std::string& combo_defines, const std::string& requested_name, int value) {
    auto replace_define = [&](const std::string& name) {
        const std::string prefix = "#define " + name + " ";
        const size_t pos = combo_defines.find(prefix);
        if (pos == std::string::npos) return false;
        size_t end = combo_defines.find('\n', pos);
        if (end == std::string::npos) end = combo_defines.size();
        combo_defines.replace(pos, end - pos, prefix + std::to_string(value));
        return true;
    };

    if (replace_define(requested_name)) return;

    std::string upper = requested_name;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    if (replace_define(upper)) return;

    // Custom effects may provide a combo that is consumed by #if without a
    // Wallpaper Engine metadata comment. Defining it is harmless and preserves
    // the authored material/pass semantics.
    combo_defines += "#define " + requested_name + " " + std::to_string(value) + "\n";
}
}  // namespace

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx) {
    cJSON* base_config = config;
    cJSON* owned_base_config = nullptr;

    // Effect passes frequently point at a Wallpaper Engine material. Resolve
    // the first material pass, then layer effect-pass and instance overrides on
    // top of it just like the native runtime does.
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(config, "material");
    if (cJSON_IsString(mat_ref) && mat_ref->valuestring) {
        char abs_mat[1024];
        if (ctx.asset_mgr.resolvePath(mat_ref->valuestring, abs_mat, sizeof(abs_mat))) {
            char* material_json_str = read_file_to_string(abs_mat);
            if (material_json_str) {
                cJSON* mat_json = cJSON_Parse(material_json_str);
                free(material_json_str);
                if (mat_json) {
                    cJSON* passes = cJSON_GetObjectItemCaseSensitive(mat_json, "passes");
                    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) {
                        owned_base_config = cJSON_Duplicate(cJSON_GetArrayItem(passes, 0), 1);
                    } else {
                        owned_base_config = cJSON_Duplicate(mat_json, 1);
                    }
                    cJSON_Delete(mat_json);
                    if (owned_base_config) base_config = owned_base_config;
                }
            }
        }
    }

    cJSON* shader_node = cJSON_GetObjectItemCaseSensitive(base_config, "shader");
    if (cJSON_IsString(shader_node) && shader_node->valuestring) shader_name = shader_node->valuestring;

    // A pass can override fields supplied by its material.
    if (base_config != config) {
        cJSON* pass_shader = cJSON_GetObjectItemCaseSensitive(config, "shader");
        if (cJSON_IsString(pass_shader) && pass_shader->valuestring) shader_name = pass_shader->valuestring;
    }

    mergeJsonObject(constant_values, cJSON_GetObjectItemCaseSensitive(base_config, "constantshadervalues"));
    readCombos(base_config, combos);
    pass_textures.loadFromConfig(base_config, shader_name, ctx);

    if (base_config != config) {
        mergeJsonObject(constant_values, cJSON_GetObjectItemCaseSensitive(config, "constantshadervalues"));
        readCombos(config, combos);
        pass_textures.applyInstanceOverrides(config, shader_name, ctx);
    }

    if (instance_config) {
        pass_textures.applyInstanceOverrides(instance_config, shader_name, ctx);
        mergeJsonObject(constant_values, cJSON_GetObjectItemCaseSensitive(instance_config, "constantshadervalues"));
        readCombos(instance_config, combos);

        cJSON* pass_enabled = cJSON_GetObjectItemCaseSensitive(instance_config, "enabled");
        if (cJSON_IsBool(pass_enabled)) enabled = cJSON_IsTrue(pass_enabled);
    }

    if (constant_values) {
        cJSON* item;
        cJSON_ArrayForEach(item, constant_values) {
            if (!item->string) continue;
            std::vector<float> values;
            if (jsonToFloats(item, values) && !values.empty()) uniforms[item->string] = std::move(values);
        }
    }

    if (owned_base_config) cJSON_Delete(owned_base_config);
}

ShaderPass::~ShaderPass() {
    if (constant_values) cJSON_Delete(constant_values);
}

void ShaderPass::init(EngineContext& ctx) {
    if (shader_name.empty()) {
        effect_log.warn("Skipping effect pass with no shader");
        return;
    }

    char vert_path[256], frag_path[256];
    if (shader_name.find("shaders/") == 0) {
        snprintf(vert_path, sizeof(vert_path), "%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "%s.frag", shader_name.c_str());
    } else {
        snprintf(vert_path, sizeof(vert_path), "shaders/%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "shaders/%s.frag", shader_name.c_str());
    }

    char abs_vert[1024], abs_frag[1024];
    char* vs_src = nullptr;
    char* fs_src = nullptr;

    if (ctx.asset_mgr.resolvePath(vert_path, abs_vert, sizeof(abs_vert))) {
        vs_src = read_file_to_string(abs_vert);
    } else {
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", vert_path);
        if (ctx.asset_mgr.resolvePath(extracted_path, abs_vert, sizeof(abs_vert))) {
            vs_src = read_file_to_string(abs_vert);
        }
    }

    if (ctx.asset_mgr.resolvePath(frag_path, abs_frag, sizeof(abs_frag))) {
        fs_src = read_file_to_string(abs_frag);
    } else {
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", frag_path);
        if (ctx.asset_mgr.resolvePath(extracted_path, abs_frag, sizeof(abs_frag))) {
            fs_src = read_file_to_string(abs_frag);
        }
    }

    if (!vs_src || !fs_src) {
        effect_log.warn("ShaderPass %s: missing vertex or fragment shader source", shader_name.c_str());
        if (vs_src) free(vs_src);
        if (fs_src) free(fs_src);
        return;
    }

    std::string raw_vs = vs_src;
    std::string raw_fs = fs_src;
    std::string combo_defines = ShaderSourceProcessor::extractCombos(fs_src);

    for (const auto& [name, value] : combos) setComboDefine(combo_defines, name, value);

    const bool is_depth_parallax = shader_name.find("depthparallax") != std::string::npos;
    const bool is_waterwaves = shader_name.find("waterwaves") != std::string::npos;

    // These two effects have well-known optional texture combos. Keep their
    // compatibility fallback, while every other effect is driven entirely by
    // material/pass combo data.
    if (is_depth_parallax) {
        setComboDefine(combo_defines, "MASK", 0);
        if (pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID) {
            setComboDefine(combo_defines, "MASK", 1);
        }
    } else if (is_waterwaves) {
        setComboDefine(combo_defines, "MASK", 0);
        setComboDefine(combo_defines, "TIMEOFFSET", 0);
        if (!pass_textures.textures.empty() && pass_textures.textures[0].id != SG_INVALID_ID) {
            setComboDefine(combo_defines, "MASK", 1);
        }
        if (pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID) {
            setComboDefine(combo_defines, "TIMEOFFSET", 1);
        }
    }

    // Resolve lowercase JSON keys such as `animationspeed` against the actual
    // shader declaration (for example g_AnimationSpeed) before building Sokol
    // uniform metadata.
    std::vector<std::string> shader_uniforms = extractUniformNames(raw_vs);
    std::vector<std::string> fragment_uniforms = extractUniformNames(raw_fs);
    shader_uniforms.insert(shader_uniforms.end(), fragment_uniforms.begin(), fragment_uniforms.end());

    std::map<std::string, std::vector<float>> resolved_uniforms;
    for (const auto& [name, values] : uniforms) {
        resolved_uniforms[resolveUniformName(name, shader_uniforms)] = values;
    }
    uniforms = std::move(resolved_uniforms);

    std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    std::string processed_vs = ShaderSourceProcessor::processShaderSource(raw_vs, abs_vert, ctx.asset_mgr, true);
    std::string processed_fs = ShaderSourceProcessor::processShaderSource(raw_fs, abs_frag, ctx.asset_mgr, false);

    if (is_depth_parallax) {
        // Wallpaper Engine opacity masks are painted in display/gamma space. The runtime effect expects the mask
        // intensity in linear space; without this conversion a dark brush floor such as 42/255 behaves like 16.5%
        // displacement instead of roughly 2.3%.
        const std::string mask_sample = "texSample2D(g_Texture2, v_TexCoordMask.xy).r";
        const size_t mask_sample_pos = processed_fs.find(mask_sample);
        if (mask_sample_pos != std::string::npos) {
            processed_fs.replace(mask_sample_pos, mask_sample.size(), "lwe_srgb_to_linear(" + mask_sample + ")");
            processed_fs =
                "float lwe_srgb_to_linear(float c) {\n"
                "    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);\n"
                "}\n" +
                processed_fs;
        }
    }

    std::string full_vs = prefix + combo_defines + processed_vs;
    std::string full_fs = prefix + combo_defines + processed_fs;

    stored_vs_source = full_vs;
    stored_fs_source = full_fs;

    full_fs = ShaderCompiler::applyDebugMode(full_fs, debug_view_mode);
    full_fs = ShaderCompiler::applyDebugStep(shader_name, full_fs, debug_step);

    texture_labels = ShaderSourceProcessor::extractTextureLabels(fs_src);

    compiled = ShaderCompiler::compile(shader_name, full_vs, full_fs, uniforms, (int)pass_textures.textures.size());

    free(vs_src);
    free(fs_src);

    pass_textures.buildCachedViews();

    if (compiled.pipeline.id == SG_INVALID_ID) {
        effect_log.warn("ShaderPass %s: effect shader could not be compiled; pass will be skipped",
                        shader_name.c_str());
        return;
    }

    // Log only effect-specific fallbacks. Extra texture meanings differ between effects.
    if (is_depth_parallax) {
        if (pass_textures.textures.empty() || pass_textures.textures[0].id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture1 (depth) missing, using Wallpaper Engine black fallback",
                            shader_name.c_str());
        }
        if (pass_textures.textures.size() < 2 || pass_textures.textures[1].id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture2 (mask) missing, using full white fallback", shader_name.c_str());
        }
    } else if (is_waterwaves) {
        if (pass_textures.textures.empty() || pass_textures.textures[0].id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture1 (mask) missing, using full white fallback", shader_name.c_str());
        }
    }
}

void ShaderPass::apply(EngineContext& ctx) {
    (void)ctx;
    if (!enabled || compiled.pipeline.id == SG_INVALID_ID) return;
}

void ShaderPass::applyUniforms() {
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        (void)name;
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        alignas(16) float data[4] = {0, 0, 0, 0};
        for (int i = 0; i < (int)vals.size() && i < 4; i++) data[i] = vals[i];
        sg_range range = SG_RANGE(data);
        sg_apply_uniforms(u_idx, &range);
        u_idx++;
    }
}

bool ShaderPass::resolveDepth(const char* source_tex_path, EngineContext& ctx) {
    const bool first_attempt = !pass_textures.depth_attempted;
    const bool resolved = pass_textures.resolveDepth(source_tex_path, shader_name, ctx);

    if (resolved || !first_attempt || shader_name.find("depthparallax") == std::string::npos) return resolved;

    const bool has_depth = !pass_textures.textures.empty() && pass_textures.textures[0].id != SG_INVALID_ID;
    const bool has_mask = pass_textures.textures.size() > 1 && pass_textures.textures[1].id != SG_INVALID_ID;
    if (!has_depth && has_mask) {
        auto center = uniforms.find("g_Center");
        if (center != uniforms.end() && !center->second.empty()) {
            center->second[0] = 0.0f;
            effect_log.info(
                "ShaderPass %s: depth map unavailable; disabling unmasked focal-plane shift while preserving masked "
                "parallax",
                shader_name.c_str());
        }
    }

    return resolved;
}

void ShaderPass::rebuildWithDebugMode(int mode, EngineContext& ctx) {
    debug_view_mode = mode;
    compiled = {};
    init(ctx);
}

Effect::Effect(cJSON* config, EngineContext& ctx) {
    cJSON* passes_node = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(passes_node)) {
        cJSON* pass_json;
        cJSON_ArrayForEach(pass_json, passes_node) {
            passes.push_back(new ShaderPass(pass_json, nullptr, ctx));
        }
    }
}

Effect::~Effect() {
    for (auto p : passes) delete p;
    passes.clear();
}

Effect* Effect::load(const char* rel_path, cJSON* instance_config, EngineContext& ctx) {
    if (!rel_path || !rel_path[0]) return nullptr;

    char abs_path[1024];
    if (!ctx.asset_mgr.resolvePath(rel_path, abs_path, sizeof(abs_path))) {
        effect_log.warn("Effect definition not found: %s", rel_path);
        return nullptr;
    }

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* config = cJSON_Parse(json_str);
    free(json_str);
    if (!config) {
        effect_log.warn("Failed to parse effect definition: %s", rel_path);
        return nullptr;
    }

    Effect* effect = new Effect(config, ctx);
    effect->file_path = rel_path;

    // Instance pass data overrides the matching pass from the effect definition.
    cJSON* inst_passes = cJSON_GetObjectItemCaseSensitive(instance_config, "passes");
    cJSON* config_passes = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(inst_passes) && cJSON_IsArray(config_passes)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_passes); i++) {
            if (i >= (int)effect->passes.size() || i >= cJSON_GetArraySize(config_passes)) break;
            cJSON* pass_config = cJSON_GetArrayItem(config_passes, i);
            cJSON* inst_pass_config = cJSON_GetArrayItem(inst_passes, i);
            delete effect->passes[i];
            effect->passes[i] = new ShaderPass(pass_config, inst_pass_config, ctx);
        }
    }

    cJSON* vis = cJSON_GetObjectItemCaseSensitive(instance_config, "visible");
    if (cJSON_IsBool(vis))
        effect->visible = cJSON_IsTrue(vis);
    else if (cJSON_IsObject(vis)) {
        cJSON* val = cJSON_GetObjectItemCaseSensitive(vis, "value");
        if (cJSON_IsBool(val)) effect->visible = cJSON_IsTrue(val);
    }

    effect->init(ctx);
    cJSON_Delete(config);

    if (effect->passes.empty()) {
        effect_log.warn("Effect %s has no render passes", rel_path);
    } else {
        effect_log.info("Loaded generic Wallpaper Engine effect: %s (%zu pass%s)", rel_path, effect->passes.size(),
                        effect->passes.size() == 1 ? "" : "es");
    }
    return effect;
}

Effect* Effect::loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx) {
    cJSON* inst_json = nullptr;
    if (!doc.instance_config_json.empty()) inst_json = cJSON_Parse(doc.instance_config_json.c_str());

    Effect* eff = load(doc.file.c_str(), inst_json, ctx);
    if (eff) eff->visible = doc.visible;
    if (inst_json) cJSON_Delete(inst_json);
    return eff;
}

void Effect::init(EngineContext& ctx) {
    for (auto p : passes) p->init(ctx);
}

void Effect::apply(EngineContext& ctx) {
    for (auto p : passes) p->apply(ctx);
}
