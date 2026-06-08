#include "effect.h"

#include "../core/context.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "imgui.h"

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config) {
    constant_values = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(config, "constantshadervalues"), 1);
    cJSON* shader_node = cJSON_GetObjectItemCaseSensitive(config, "shader");
    if (cJSON_IsString(shader_node)) {
        shader_name = shader_node->valuestring;
    }

    // Load base textures
    cJSON* textures_node = cJSON_GetObjectItemCaseSensitive(config, "textures");
    if (cJSON_IsArray(textures_node)) {
        cJSON* tex_node;
        cJSON_ArrayForEach(tex_node, textures_node) {
            if (cJSON_IsString(tex_node)) {
                std::string path;
                sg_image img = state.asset_mgr.resolveTexture(tex_node->valuestring, &path);
                textures.push_back(img);
                texture_paths.push_back(path);
                texture_masks.push_back(true);
                effect_log.info("ShaderPass %s: Loaded texture %d: %s (id: %d)", shader_name.c_str(),
                                (int)textures.size() - 1, path.c_str(), img.id);
            } else {
                // Auto-resolve depth map if it's the second slot and previous was a .tex
                if (textures.size() == 1 && !texture_paths[0].empty() && strstr(texture_paths[0].c_str(), ".tex")) {
                    std::string path;
                    sg_image img = state.asset_mgr.resolveTexture(texture_paths[0].c_str(), &path, 1);
                    if (img.id != SG_INVALID_ID) {
                        textures.push_back(img);
                        texture_paths.push_back(path + "#1");
                        texture_masks.push_back(true);
                        effect_log.info("ShaderPass %s: Auto-resolved texture 1 from %s (index 1, id: %d)",
                                        shader_name.c_str(), texture_paths[0].c_str(), img.id);
                        continue;
                    }
                }
                textures.push_back((sg_image){SG_INVALID_ID});
                texture_paths.push_back("");
                texture_masks.push_back(true);
            }
        }
    }

    // Apply instance overrides from scene.json
    if (instance_config) {
        cJSON* inst_textures = cJSON_GetObjectItemCaseSensitive(instance_config, "textures");
        if (cJSON_IsArray(inst_textures)) {
            for (int i = 0; i < cJSON_GetArraySize(inst_textures); i++) {
                cJSON* tex_node = cJSON_GetArrayItem(inst_textures, i);
                if (cJSON_IsString(tex_node)) {
                    std::string path;
                    sg_image img = state.asset_mgr.resolveTexture(tex_node->valuestring, &path);
                    if (img.id != SG_INVALID_ID) {
                        if (i < (int)textures.size()) {
                            // Replace existing slot
                            if (textures[i].id != SG_INVALID_ID) sg_destroy_image(textures[i]);
                            textures[i] = img;
                            texture_paths[i] = path;
                            effect_log.info("ShaderPass %s: Overrode texture %d: %s (id: %d)", shader_name.c_str(), i,
                                            path.c_str(), img.id);
                        } else {
                            // Expand and add new slot
                            while ((int)textures.size() < i) {
                                textures.push_back((sg_image){SG_INVALID_ID});
                                texture_paths.push_back("");
                                texture_masks.push_back(true);
                            }
                            textures.push_back(img);
                            texture_paths.push_back(path);
                            texture_masks.push_back(true);
                            effect_log.info("ShaderPass %s: Added texture %d: %s (id: %d)", shader_name.c_str(), i,
                                            path.c_str(), img.id);
                        }
                    }
                }
            }
        }
        // Merge constants
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

    // Parse uniforms from constants
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
                // Map common Wallpaper Engine uniform names to g_ prefixed ones
                std::string name = item->string;
                if (name == "scale")
                    name = "g_Scale";
                else if (name == "sens")
                    name = "g_Sensitivity";
                else if (name == "center")
                    name = "g_Center";
                else if (name.find("g_") != 0)
                    name = "g_" + name;

                uniforms[name] = vals;
            }
        }
    }
}

ShaderPass::~ShaderPass() {
    if (constant_values) cJSON_Delete(constant_values);
    if (pipeline.id != SG_INVALID_ID) sg_destroy_pipeline(pipeline);
    if (shader.id != SG_INVALID_ID) sg_destroy_shader(shader);
}

void ShaderPass::init() {
    if (shader_name.empty()) return;

    effect_log.info("Initializing ShaderPass: %s", shader_name.c_str());

    char vert_path[256], frag_path[256];
    snprintf(vert_path, sizeof(vert_path), "shaders/%s.vert", shader_name.c_str());
    snprintf(frag_path, sizeof(frag_path), "shaders/%s.frag", shader_name.c_str());

    char abs_vert[1024], abs_frag[1024];
    char* vs_src = nullptr;
    char* fs_src = nullptr;

    if (state.asset_mgr.resolvePath(vert_path, abs_vert, sizeof(abs_vert))) {
        vs_src = read_file_to_string(abs_vert);
    }
    if (state.asset_mgr.resolvePath(frag_path, abs_frag, sizeof(abs_frag))) {
        fs_src = read_file_to_string(abs_frag);
    }

    if (!vs_src || !fs_src) {
        effect_log.error("Failed to load shader sources for %s", shader_name.c_str());
        if (vs_src) free(vs_src);
        if (fs_src) free(fs_src);
        return;
    }

    const char* shader_prefix =
        "#version 330\n"
        "#define mul(v, m) (m * v)\n"
        "#define texSample2D(s, uv) texture(s, uv)\n"
        "#define CAST2(x) vec2(x)\n"
        "#define CAST3(x) vec3(x)\n"
        "#define CAST4(x) vec4(x)\n"
        "#define CAST3X3(x) mat3(x)\n"
        "#define saturate(x) clamp(x, 0.0, 1.0)\n"
        "#define lerp mix\n"
        "precision mediump float;\n";

    std::string full_vs = shader_prefix + std::string(vs_src);
    std::string full_fs = shader_prefix + std::string(fs_src);

    sg_shader_desc shd_desc = {};
    shd_desc.vertex_func.source = full_vs.c_str();
    shd_desc.fragment_func.source = full_fs.c_str();

    // Slot 0: Built-in Uniforms (WPE style)
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrix";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[1].size = sizeof(float) * 4 * 4 + sizeof(float) * 4;
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "g_Texture1Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "g_ParallaxPosition";
    shd_desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;

    // Slot 2: Fragment Tint
    shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[2].size = sizeof(float) * 4;
    shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "tint";
    shd_desc.uniform_blocks[2].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    // Custom uniforms (up to 8 slots total in Sokol)
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        shd_desc.uniform_blocks[u_idx].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.uniform_blocks[u_idx].size = sizeof(float) * 4;
        shd_desc.uniform_blocks[u_idx].glsl_uniforms[0].glsl_name = name.c_str();
        shd_desc.uniform_blocks[u_idx].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
        u_idx++;
    }

    // Samplers
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].glsl_name = "g_Texture0";
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;

    for (int i = 0; i < (int)textures.size() && i < 11; i++) {
        char tex_name[32];
        snprintf(tex_name, sizeof(tex_name), "g_Texture%d", i + 1);
        shd_desc.views[i + 1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[i + 1].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.texture_sampler_pairs[i + 1].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[i + 1].glsl_name = tex_name;
        shd_desc.texture_sampler_pairs[i + 1].view_slot = i + 1;
        shd_desc.texture_sampler_pairs[i + 1].sampler_slot = 0;  // Use same sampler
    }

    shader = sg_make_shader(&shd_desc);
    free(vs_src);
    free(fs_src);

    if (shader.id == SG_INVALID_ID) {
        effect_log.error("Failed to create shader for %s", shader_name.c_str());
        return;
    }

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shader;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline = sg_make_pipeline(&pip_desc);

    if (pipeline.id == SG_INVALID_ID) {
        effect_log.error("Failed to create pipeline for %s", shader_name.c_str());
    } else {
        effect_log.info("Created pipeline for %s", shader_name.c_str());
    }
}

void ShaderPass::apply() {
    if (!enabled || pipeline.id == SG_INVALID_ID) return;
}

void ShaderPass::applyUniforms() {
    int u_idx = 2;
    for (auto const& [name, vals] : uniforms) {
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        float data[4] = {0, 0, 0, 0};
        for (int i = 0; i < (int)vals.size() && i < 4; i++) data[i] = vals[i];
        sg_range range = SG_RANGE(data);
        sg_apply_uniforms(u_idx, &range);
        u_idx++;
    }
}

void ShaderPass::showInspector(int id) {
    ImGui::PushID(id);
    ImGui::Checkbox(shader_name.empty() ? "Pass" : shader_name.c_str(), &enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle this specific shader pass");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Files")) show_files = !show_files;

    if (show_files) {
        ImGui::Indent();
        ImGui::Text("Textures (%d):", (int)textures.size());
        for (int i = 0; i < (int)textures.size(); i++) {
            ImGui::PushID(i);
            if (i < (int)texture_masks.size()) {
                bool m = texture_masks[i];
                if (ImGui::Checkbox("##mask", &m)) texture_masks[i] = m;
                ImGui::SameLine();
            }
            if (!texture_paths[i].empty()) {
                ImGui::Text("Slot %d: %s", i + 1, texture_paths[i].c_str());
            } else {
                ImGui::Text("Slot %d: [Empty]", i + 1);
            }
            ImGui::PopID();
        }
        ImGui::Unindent();
    }
    ImGui::PopID();
}

Effect::Effect(cJSON* config) {
    cJSON* passes_node = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(passes_node)) {
        cJSON* pass_json;
        cJSON_ArrayForEach(pass_json, passes_node) {
            passes.push_back(new ShaderPass(pass_json, nullptr));
        }
    }
}

Effect::~Effect() {
    for (auto p : passes) delete p;
    passes.clear();
}

Effect* Effect::load(const char* rel_path, cJSON* instance_config) {
    char abs_path[1024];
    if (!state.asset_mgr.resolvePath(rel_path, abs_path, sizeof(abs_path))) return nullptr;

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* config = cJSON_Parse(json_str);
    free(json_str);
    if (!config) return nullptr;

    Effect* effect = new Effect(config);
    effect->file_path = rel_path;

    // Apply instance overrides to passes
    cJSON* inst_passes = cJSON_GetObjectItemCaseSensitive(instance_config, "passes");
    if (cJSON_IsArray(inst_passes)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_passes); i++) {
            if (i < (int)effect->passes.size()) {
                cJSON* pass_config = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(config, "passes"), i);
                cJSON* inst_pass_config = cJSON_GetArrayItem(inst_passes, i);
                delete effect->passes[i];
                effect->passes[i] = new ShaderPass(pass_config, inst_pass_config);
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

    effect->init();

    cJSON_Delete(config);
    return effect;
}

void Effect::init() {
    for (auto p : passes) p->init();
}

void Effect::apply() {
    for (auto p : passes) p->apply();
}

void Effect::showInspector(int id) {
    ImGui::PushID(id);

    // Visibility Toggle
    if (ImGui::Button(visible ? "V" : " ", ImVec2(25, 0))) {
        visible = !visible;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Effect Visibility");

    ImGui::SameLine();

    // Solo Toggle
    if (solo) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
    }
    if (ImGui::Button("S", ImVec2(25, 0))) {
        solo = !solo;
    }
    if (solo) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo Effect (Ctrl+Click effect name also works)");

    ImGui::SameLine();

    std::string effect_name = "Unknown Effect";
    if (!passes.empty() && !passes[0]->shader_name.empty()) {
        effect_name = passes[0]->shader_name;
    } else {
        size_t last_slash = file_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string sub = file_path.substr(0, last_slash);
            size_t prev_slash = sub.find_last_of('/');
            if (prev_slash != std::string::npos) {
                effect_name = sub.substr(prev_slash + 1);
            } else {
                effect_name = sub;
            }
        } else {
            effect_name = file_path;
        }
    }

    if (!effect_name.empty()) {
        effect_name[0] = toupper(effect_name[0]);
    }

    bool open = ImGui::TreeNodeEx(effect_name.c_str(), ImGuiTreeNodeFlags_FramePadding);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s (Ctrl+Click to toggle Solo)", file_path.c_str());
    }
    if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
        solo = !solo;
    }

    if (open) {
        ImGui::Indent();
        for (int i = 0; i < (int)passes.size(); i++) {
            passes[i]->showInspector(i);
        }
        ImGui::Unindent();
        ImGui::TreePop();
    }
    ImGui::PopID();
}
