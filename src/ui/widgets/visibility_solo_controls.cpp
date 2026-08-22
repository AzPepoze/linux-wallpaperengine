#include "visibility_solo_controls.h"

#include "imgui.h"
#include "wallpaper/2d/layers/layer.h"

namespace UiWidgets {

void drawVisibilitySoloControls(bool& visible, bool& solo, const char* visibility_tooltip, const char* solo_tooltip) {
    if (ImGui::Button(visible ? "V" : " ", ImVec2(25.0f, 0.0f))) visible = !visible;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", visibility_tooltip);

    ImGui::SameLine();
    const bool was_solo = solo;
    if (was_solo) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
    }
    if (ImGui::Button("S", ImVec2(25.0f, 0.0f))) solo = !solo;
    if (was_solo) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", solo_tooltip);
}

void drawVisibilitySoloControls(Layer& layer, const char* visibility_tooltip, const char* solo_tooltip) {
    if (ImGui::Button(layer.is_visible() ? "V" : " ", ImVec2(25.0f, 0.0f))) {
        layer.setVisible(!layer.is_visible());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", visibility_tooltip);

    ImGui::SameLine();
    const bool was_solo = layer.solo;
    if (was_solo) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
    }
    if (ImGui::Button("S", ImVec2(25.0f, 0.0f))) layer.setSolo(!layer.solo);
    if (was_solo) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", solo_tooltip);
}

}  // namespace UiWidgets
