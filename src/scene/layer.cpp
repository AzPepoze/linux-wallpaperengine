#include "layer.h"

#include "../core/context.h"
#include "imgui.h"

void Layer::loadBaseProperties(cJSON* node) {
    cJSON* name_node = cJSON_GetObjectItemCaseSensitive(node, "name");
    if (cJSON_IsString(name_node)) name = name_node->valuestring;

    cJSON* origin_node = cJSON_GetObjectItemCaseSensitive(node, "origin");
    if (cJSON_IsString(origin_node)) {
        sscanf(origin_node->valuestring, "%f %f %f", &origin[0], &origin[1], &origin[2]);
    }

    cJSON* scale_node = cJSON_GetObjectItemCaseSensitive(node, "scale");
    if (cJSON_IsString(scale_node)) {
        sscanf(scale_node->valuestring, "%f %f %f", &scale[0], &scale[1], &scale[2]);
    }

    cJSON* parallax_node = cJSON_GetObjectItemCaseSensitive(node, "parallax");
    if (!parallax_node) parallax_node = cJSON_GetObjectItemCaseSensitive(node, "parallaxDepth");
    if (cJSON_IsString(parallax_node)) {
        sscanf(parallax_node->valuestring, "%f %f", &parallax[0], &parallax[1]);
    }

    cJSON* visible_node = cJSON_GetObjectItemCaseSensitive(node, "visible");
    if (cJSON_IsBool(visible_node)) {
        visible = cJSON_IsTrue(visible_node);
    } else if (cJSON_IsObject(visible_node)) {
        cJSON* val = cJSON_GetObjectItemCaseSensitive(visible_node, "value");
        if (cJSON_IsBool(val)) visible = cJSON_IsTrue(val);
    }

    // Load Effects
    cJSON* effects_node = cJSON_GetObjectItemCaseSensitive(node, "effects");
    if (cJSON_IsArray(effects_node)) {
        cJSON* eff_json;
        cJSON_ArrayForEach(eff_json, effects_node) {
            cJSON* file_node = cJSON_GetObjectItemCaseSensitive(eff_json, "file");
            if (cJSON_IsString(file_node)) {
                Effect* effect = Effect::load(file_node->valuestring, eff_json);
                if (effect) effects.push_back(effect);
            }
        }
    }
}

void Layer::showBaseInspector() {
    ImGui::Checkbox("Visible", &visible);
    ImGui::SameLine();
    ImGui::Checkbox("Solo", &solo);
}

void Layer::showEffectsInspector() {
    if (effects.empty()) return;

    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)effects.size(); i++) {
            effects[i]->showInspector(i);
        }
    }
}
