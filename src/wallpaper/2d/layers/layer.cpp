#include "layer.h"

#if DEBUG_BUILD
#include "imgui.h"
#include "wallpaper/2d/effects/effect_inspector.h"
#include "wallpaper/2d/tree/scene_tree.h"
#endif

void Layer::initFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    if (doc.node.valid && doc.node.id > 0) {
        scene_object_id = doc.node.id;
    }
    name = doc.name;
    visible = doc.visible;
    origin[0] = doc.node.origin[0];
    origin[1] = doc.node.origin[1];
    origin[2] = doc.node.origin[2];
    scale[0] = doc.node.scale[0];
    scale[1] = doc.node.scale[1];
    scale[2] = doc.node.scale[2];
    rotation = doc.node.angles[2];
    parallax[0] = doc.node.parallax_depth[0];
    parallax[1] = doc.node.parallax_depth[1];

    for (const auto& eff_doc : doc.effects) {
        Effect* effect = Effect::loadFromDocument(eff_doc, ctx);
        if (effect) effects.push_back(effect);
    }
}

#if DEBUG_BUILD
void Layer::showGeneralInspector(EngineContext& ctx) {
    bool is_vis = is_visible();
    if (ImGui::Checkbox("Visible", &is_vis)) {
        setVisible(is_vis);
    }
    ImGui::SameLine();
    bool is_solo = solo;
    if (ImGui::Checkbox("Solo", &is_solo)) {
        setSolo(is_solo);
    }

    SceneTreeNode* node = (scene_object_id != 0 && ctx.scene_tree) ? ctx.scene_tree->find(scene_object_id) : nullptr;

    float* pos_ptr = node ? node->origin.data() : (float*)origin;
    if (ImGui::DragFloat3("Position", pos_ptr, 1.0f)) {
        if (node) {
            origin[0] = node->origin[0];
            origin[1] = node->origin[1];
            origin[2] = node->origin[2];
        }
    }

    float* scale_ptr = node ? node->scale.data() : (float*)scale;
    if (ImGui::DragFloat3("Scale", scale_ptr, 0.01f, 0.001f, 100.0f)) {
        if (node) {
            scale[0] = node->scale[0];
            scale[1] = node->scale[1];
            scale[2] = node->scale[2];
        }
    }

    float rot = node ? node->angles[2] : rotation;
    if (ImGui::DragFloat("Rotation", &rot, 1.0f, -360.0f, 360.0f)) {
        if (node) node->angles[2] = rot;
        rotation = rot;
    }

    float* parallax_ptr = node ? node->parallax_depth.data() : (float*)parallax;
    if (ImGui::DragFloat2("Parallax Depth", parallax_ptr, 0.01f, -10.0f, 10.0f)) {
        if (node) {
            parallax[0] = node->parallax_depth[0];
            parallax[1] = node->parallax_depth[1];
        }
    }
}

void Layer::showEffectsInspector(EngineContext& ctx) {
    if (effects.empty()) return;
    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)effects.size(); i++) Inspector::showEffect(ctx, *effects[i], i);
    }
}

void Layer::showInspector(EngineContext& ctx) {
    showGeneralInspector(ctx);
    showEffectsInspector(ctx);
}
#endif
