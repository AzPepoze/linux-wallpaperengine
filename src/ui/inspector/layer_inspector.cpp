#include "layer_inspector.h"
#include "../../scene/layer.h"
#include "../../scene/2d/image_layer.h"
#include "../../scene/2d/particle_layer.h"
#include "../../scene/2d/particles.h"
#include "effect_inspector.h"
#include "imgui.h"

namespace Inspector {

static void showBaseInspector(::Layer& layer) {
    ImGui::Checkbox("Visible", &layer.visible);
    ImGui::SameLine();
    ImGui::Checkbox("Solo", &layer.solo);
}

static void showEffectsInspector(EngineContext& ctx, ::Layer& layer) {
    if (layer.effects.empty()) return;

    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)layer.effects.size(); i++) {
            showEffect(ctx, *layer.effects[i], i);
        }
    }
}

static void showParticleSystem(::ParticleSystem& ps) {
    ImGui::Text("Active Particles: %d / %d", (int)ps.particles.size(), ps.max_particles);
    if (!ps.config_path.empty()) ImGui::Text("Config: %s", ps.config_path.c_str());
    if (!ps.texture_path.empty()) ImGui::Text("Texture: %s", ps.texture_path.c_str());
    ImGui::Text("Blending: %s", ps.is_additive ? "Additive" : "Alpha");
    ImGui::Text("Children: %d", (int)ps.children.size());
}

void showLayer(EngineContext& ctx, ::Layer& layer) {
    showBaseInspector(layer);

    if (auto* il = dynamic_cast<::ImageLayer*>(&layer)) {
        ImGui::Text("Type: Image");
        if (!il->path.empty()) {
            ImGui::Text("Path: %s", il->path.c_str());
        }

        ImGui::Separator();
        ImGui::DragFloat3("Position", (float*)il->origin, 1.0f);
        ImGui::DragFloat3("Scale", (float*)il->scale, 0.01f);
        ImGui::DragFloat2("Size", (float*)il->size, 1.0f);
        ImGui::DragFloat("Rotation", &il->rotation, 1.0f, 0, 360);
        ImGui::ColorEdit4("Tint", il->tint);
        ImGui::DragFloat2("Parallax", (float*)il->parallax, 0.01f, -10, 10);
    } else if (auto* pl = dynamic_cast<::ParticleLayer*>(&layer)) {
        ImGui::Text("Type: Particle System");
        if (!pl->path.empty()) {
            ImGui::Text("Path: %s", pl->path.c_str());
        }

        ImGui::Separator();
        if (pl->ps) showParticleSystem(*pl->ps);

        ImGui::Separator();
        ImGui::DragFloat3("Position", (float*)pl->origin, 1.0f);
        ImGui::DragFloat2("Parallax", (float*)pl->parallax, 0.01f, -10, 10);
    }

    showEffectsInspector(ctx, layer);
}

} // namespace Inspector
