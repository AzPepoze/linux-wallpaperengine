#include "effect.h"

#include <fstream>
#include <sstream>

#include "../../libs/sokol/sokol_app.h"
#include "../core/config.h"
#include "../core/context.h"
#include "../core/engine_context.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "shader_processor.h"

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx) {
    cJSON* base_config = config;
    char* material_json_str = nullptr;

    // If config has a material reference, load that instead
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(config, "material");
    if (cJSON_IsString(mat_ref)) {
        char abs_mat[1024];
        if (ctx.asset_mgr.resolvePath(mat_ref->valuestring, abs_mat, sizeof(abs_mat))) {
            material_json_str = read_file_to_string(abs_mat);
            if (material_json_str) {
                cJSON* mat_json = cJSON_Parse(material_json_str);
                if (mat_json) {
                    cJSON* passes = cJSON_GetObjectItemCaseSensitive(mat_json, "passes");
                    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) {
                        base_config = cJSON_Duplicate(cJSON_GetArrayItem(passes, 0), 1);
                        cJSON_Delete(mat_json);
                    } else {
                        base_config = mat_json;
                    }
                }
            }
        }
    }

    constant_values = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(base_config, "constantshadervalues"), 1);
    cJSON* shader_node = cJSON_GetObjectItemCaseSensitive(base_config, "shader");
    if (cJSON_IsString(shader_node)) {
        shader_name = shader_node->valuestring;
    }

    pass_textures.loadFromConfig(base_config, shader_name, ctx);

    if (material_json_str) {
        cJSON_Delete(base_config);
        free(material_json_str);
    }

    if (instance_config) {
        pass_textures.applyInstanceOverrides(instance_config, shader_name, ctx);

        cJSON* inst_const = cJSON_GetObjectItemCaseSensitive(instance_config, "constantshadervalues");
        if (cJSON_IsObject(inst_const)) {
            if (!constant_values)
                constant_values = cJSON_Duplicate(inst_const, 1);
            else {
                cJSON* item;
                cJSON_ArrayForEach(item, inst_const) {
                    cJSON* existing = cJSON_GetObjectItemCaseSensitive(constant_values, item->string);
                    if (existing) {
                        cJSON_DeleteItemFromObjectCaseSensitive(constant_values, item->string);
                    }
                    cJSON_AddItemToObject(constant_values, item->string, cJSON_Duplicate(item, 1));
                }
            }
        }
    }

    if (constant_values) {
        cJSON* item;
        cJSON_ArrayForEach(item, constant_values) {
            std::vector<float> vals;
            if (cJSON_IsNumber(item)) {
                vals.push_back((float)item->valuedouble);
            } else if (cJSON_IsString(item)) {
                float f1, f2, f3, f4;
                int count = sscanf(item->valuestring, "%f %f %f %f", &f1, &f2, &f3, &f4);
                if (count >= 1) vals.push_back(f1);
                if (count >= 2) vals.push_back(f2);
                if (count >= 3) vals.push_back(f3);
                if (count >= 4) vals.push_back(f4);
            }
            if (!vals.empty()) {
                std::string name = item->string;
                if (Config::kUniformNameMap.count(name)) {
                    name = Config::kUniformNameMap.at(name);
                } else if (name.find("g_") != 0) {
                    std::string mapped = "g_";
                    mapped += (char)toupper(name[0]);
                    mapped += name.substr(1);
                    name = mapped;
                }
                uniforms[name] = vals;
            }
        }
    }
}

ShaderPass::~ShaderPass() {
    if (constant_values) cJSON_Delete(constant_values);
}

void ShaderPass::init(EngineContext& ctx) {
    if (shader_name.empty()) return;

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
        if (vs_src) free(vs_src);
        if (fs_src) free(fs_src);
        return;
    }

    std::string combo_defines = ShaderSourceProcessor::extractCombos(fs_src);
    if (constant_values) {
        cJSON* item;
        cJSON_ArrayForEach(item, constant_values) {
            if (cJSON_IsNumber(item)) {
                std::string name = item->string;
                for (auto& c : name) c = toupper(c);
                combo_defines += "#define " + name + " " + std::to_string((int)item->valuedouble) + "\n";
            }
        }
    }

    for (int i = 0; i < (int)pass_textures.textures.size(); i++) {
        if (pass_textures.textures[i].id != SG_INVALID_ID && pass_textures.texture_masks[i]) {
            if (i == 1) combo_defines += "#define MASK 1\n";
            if (i == 2) combo_defines += "#define TIMEOFFSET 1\n";
        }
    }

    std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    std::string processed_vs = ShaderSourceProcessor::processShaderSource(vs_src, true);
    std::string processed_fs = ShaderSourceProcessor::processShaderSource(fs_src, false);

    std::string full_vs = prefix + combo_defines + processed_vs;
    std::string full_fs = prefix + combo_defines + processed_fs;

    stored_vs_source = full_vs;
    stored_fs_source = full_fs;

    full_fs = ShaderCompiler::applyDebugMode(full_fs, debug_view_mode);
    full_fs = ShaderCompiler::applyDebugStep(shader_name, full_fs, debug_step);

    texture_labels = ShaderSourceProcessor::extractTextureLabels(fs_src);

    compiled = ShaderCompiler::compile(shader_name, full_vs, full_fs, uniforms, pass_textures.textures.size());

    free(vs_src);
    free(fs_src);

    pass_textures.buildCachedViews();

    // Log fallbacks
    if (pass_textures.textures.empty() || pass_textures.textures[0].id == SG_INVALID_ID) {
        effect_log.warn("ShaderPass %s: g_Texture1 (depth) missing, using neutral gray fallback", shader_name.c_str());
    }
    if (pass_textures.textures.size() < 2 || pass_textures.textures[1].id == SG_INVALID_ID) {
        effect_log.warn("ShaderPass %s: g_Texture2 (mask) missing, using full white fallback", shader_name.c_str());
    }
}

void ShaderPass::apply(EngineContext& ctx) {
    if (!enabled || compiled.pipeline.id == SG_INVALID_ID) return;
}

void ShaderPass::applyUniforms() {
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        float data[4] = {0, 0, 0, 0};
        for (int i = 0; i < (int)vals.size() && i < 4; i++) data[i] = vals[i];
        sg_range range = SG_RANGE(data);
        sg_apply_uniforms(u_idx, &range);
        u_idx++;
    }
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
    if (strstr(rel_path, "depthparallax") == nullptr) {
        effect_log.warn("Ignoring unsupported effect: %s", rel_path);
        return nullptr;
    }

    char abs_path[1024];
    if (!ctx.asset_mgr.resolvePath(rel_path, abs_path, sizeof(abs_path))) return nullptr;

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* config = cJSON_Parse(json_str);
    free(json_str);
    if (!config) return nullptr;

    Effect* effect = new Effect(config, ctx);
    effect->file_path = rel_path;

    cJSON* inst_passes = cJSON_GetObjectItemCaseSensitive(instance_config, "passes");
    if (cJSON_IsArray(inst_passes)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_passes); i++) {
            if (i < (int)effect->passes.size()) {
                cJSON* pass_config = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(config, "passes"), i);
                cJSON* inst_pass_config = cJSON_GetArrayItem(inst_passes, i);
                delete effect->passes[i];
                effect->passes[i] = new ShaderPass(pass_config, inst_pass_config, ctx);
            }
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
    return effect;
}

void Effect::init(EngineContext& ctx) {
    for (auto p : passes) p->init(ctx);
}

void Effect::apply(EngineContext& ctx) {
    for (auto p : passes) p->apply(ctx);
}
