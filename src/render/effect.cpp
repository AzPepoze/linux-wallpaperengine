#include "effect.h"

#include "../core/context.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "imgui.h"

ShaderPass::ShaderPass(cJSON* config) {
    constant_values = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(config, "constantshadervalues"), 1);
    cJSON* shader_node = cJSON_GetObjectItemCaseSensitive(config, "shader");
    if (cJSON_IsString(shader_node)) {
        shader_name = shader_node->valuestring;
    }

    cJSON* textures_node = cJSON_GetObjectItemCaseSensitive(config, "textures");
    if (cJSON_IsArray(textures_node)) {
        cJSON* tex_node;
        cJSON_ArrayForEach(tex_node, textures_node) {
            if (cJSON_IsString(tex_node)) {
                sg_image img = state.asset_mgr.resolveTexture(tex_node->valuestring);
                textures.push_back(img);
            } else {
                textures.push_back((sg_image){SG_INVALID_ID});
            }
        }
    }
}

ShaderPass::~ShaderPass() {
    if (constant_values) cJSON_Delete(constant_values);
}

void ShaderPass::apply() {
    if (!enabled) return;
}

void ShaderPass::showInspector(int id) {
    ImGui::PushID(id);
    ImGui::Checkbox(shader_name.empty() ? "Pass" : shader_name.c_str(), &enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle this specific shader pass");
    }
    ImGui::PopID();
}

Effect::Effect(cJSON* config) {
    cJSON* passes_node = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(passes_node)) {
        cJSON* pass_json;
        cJSON_ArrayForEach(pass_json, passes_node) {
            passes.push_back(new ShaderPass(pass_json));
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

    cJSON* vis = cJSON_GetObjectItemCaseSensitive(instance_config, "visible");
    if (cJSON_IsBool(vis)) effect->visible = cJSON_IsTrue(vis);

    cJSON_Delete(config);
    return effect;
}

void Effect::apply() {
    if (!visible) return;
    for (auto p : passes) p->apply();
}

void Effect::showInspector(int id) {
    ImGui::PushID(id);

    // Get the primary shader name from the first pass for immediate recognition
    std::string effect_name = "Unknown Effect";
    if (!passes.empty() && !passes[0]->shader_name.empty()) {
        effect_name = passes[0]->shader_name;
    } else {
        size_t last_slash = file_path.find_last_of('/');
        if (last_slash != std::string::npos)
            effect_name = file_path.substr(last_slash + 1);
        else
            effect_name = file_path;
    }

    // Use a checkbox + treenode combo for the header
    ImGui::Checkbox("##enabled", &visible);
    ImGui::SameLine();

    bool open = ImGui::TreeNodeEx(effect_name.c_str(), ImGuiTreeNodeFlags_FramePadding);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", file_path.c_str());
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
