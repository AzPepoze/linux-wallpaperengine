#include "layer_inspector.h"

#if DEBUG_BUILD

#include "effect_inspector.h"
#include "image_inspector.h"
#include "imgui.h"
#include "particle_inspector.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"
#include "wallpaper/2d/tree/scene_visibility.h"

namespace Inspector {

namespace {

void showGeneralInspector(EngineContext& ctx, Layer& layer) {
    bool is_vis = layer.is_visible();
    if (ImGui::Checkbox("Visible", &is_vis)) {
        layer.setVisible(is_vis);
    }
    ImGui::SameLine();
    bool is_solo = layer.solo;
    if (ImGui::Checkbox("Solo", &is_solo)) {
        layer.setSolo(is_solo);
    }
    if (layer.visible && !SceneVisibility(ctx).visible(layer)) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Hidden by a parent");
    }

    SceneTreeNode* node = (layer.scene_object_id != 0 && ctx.scene.scene_tree)
                              ? ctx.scene.scene_tree->find(layer.scene_object_id)
                              : nullptr;

    float* pos_ptr = node ? node->origin.data() : (float*)layer.origin;
    if (ImGui::DragFloat3("Position", pos_ptr, 1.0f)) {
        if (node) {
            layer.origin[0] = node->origin[0];
            layer.origin[1] = node->origin[1];
            layer.origin[2] = node->origin[2];
        }
    }

    float* scale_ptr = node ? node->scale.data() : (float*)layer.scale;
    if (ImGui::DragFloat3("Scale", scale_ptr, 0.01f, 0.001f, 100.0f)) {
        if (node) {
            layer.scale[0] = node->scale[0];
            layer.scale[1] = node->scale[1];
            layer.scale[2] = node->scale[2];
        }
    }

    float rot = node ? node->angles[2] : layer.rotation;
    if (ImGui::DragFloat("Rotation", &rot, 1.0f, -360.0f, 360.0f)) {
        if (node) node->angles[2] = rot;
        layer.rotation = rot;
    }

    float* parallax_ptr = node ? node->parallax_depth.data() : (float*)layer.parallax;
    if (ImGui::DragFloat2("Parallax Depth", parallax_ptr, 0.01f, -10.0f, 10.0f)) {
        if (node) {
            layer.parallax[0] = node->parallax_depth[0];
            layer.parallax[1] = node->parallax_depth[1];
        }
    }
}

void showEffectsInspector(EngineContext& ctx, Layer& layer) {
    if (layer.effects.empty()) return;
    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)layer.effects.size(); i++) showEffect(ctx, *layer.effects[i], i);
    }
}

}  // namespace

void showLayer(EngineContext& ctx, Layer& layer) {
    showGeneralInspector(ctx, layer);
    if (const auto* text = dynamic_cast<const TextLayer*>(&layer)) {
        std::string value;
        text->propertyGetString("text", value);
        ImGui::TextWrapped("Current text: %s", value.empty() ? "(empty)" : value.c_str());
        text->propertyGetString("font", value);
        ImGui::TextWrapped("Font: %s", value.c_str());
    }
    if (auto* image = dynamic_cast<ImageLayer*>(&layer)) {
        showImageLayerInspector(ctx, *image);
    } else if (auto* particle = dynamic_cast<ParticleLayer*>(&layer)) {
        showParticleLayerInspector(ctx, *particle);
    }
    showEffectsInspector(ctx, layer);
}

}  // namespace Inspector

#endif  // DEBUG_BUILD
