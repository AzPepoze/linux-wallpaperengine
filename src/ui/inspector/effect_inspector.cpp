#include "effect_inspector.h"

#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_imgui.h"
#include "../../render/effect.h"
#include "imgui.h"

namespace Inspector {

void showShaderPass(EngineContext& ctx, ::ShaderPass& pass, int id) {
    ImGui::PushID(id);

    // Pass header with enable toggle
    if (ImGui::Checkbox(pass.shader_name.empty() ? "Pass" : pass.shader_name.c_str(), &pass.enabled)) {
        // Toggle logic if needed
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle this specific shader pass");
    }

    // Debug view mode selector
    std::vector<std::string> mode_names = {"Normal"};
    for (int i = 0; i < (int)pass.pass_textures.textures.size(); i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Tex%d", i);
        mode_names.push_back(buf);
    }
    for (int i = 0; i < (int)pass.pass_textures.textures.size(); i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Tex%d (Red)", i);
        mode_names.push_back(buf);
    }

    std::vector<const char*> mode_ptrs;
    for (auto& s : mode_names) mode_ptrs.push_back(s.c_str());

    int prev_mode = pass.debug_view_mode;
    // Map current mode to index in mode_ptrs
    int current_idx = 0;
    if (pass.debug_view_mode >= 1 && pass.debug_view_mode <= 10) {
        current_idx = pass.debug_view_mode;
    } else if (pass.debug_view_mode >= 11 && pass.debug_view_mode <= 20) {
        current_idx = (pass.debug_view_mode - 11) + (int)pass.pass_textures.textures.size() + 1;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    if (ImGui::Combo("##debugview", &current_idx, mode_ptrs.data(), (int)mode_ptrs.size())) {
        if (current_idx == 0)
            pass.debug_view_mode = 0;
        else if (current_idx <= (int)pass.pass_textures.textures.size())
            pass.debug_view_mode = current_idx;
        else
            pass.debug_view_mode = (current_idx - (int)pass.pass_textures.textures.size() - 1) + 11;

        if (pass.debug_view_mode != prev_mode) {
            pass.rebuildWithDebugMode(pass.debug_view_mode, ctx);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visual debug: override fragment output to inspect shader values");

    int prev_step = pass.debug_step;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderInt("##debugstep", &pass.debug_step, 0, (int)pass.pass_textures.textures.size())) {
        if (pass.debug_step != prev_step) {
            pass.rebuildWithDebugMode(pass.debug_view_mode, ctx);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Debug Step: forced texture output (bypasses main logic)");

    ImGui::Indent();
    if (pass.pass_textures.textures.empty()) {
        ImGui::TextDisabled("[No textures for this pass]");
    } else {
        // Texture Grid Visualization
        if (ImGui::TreeNodeEx("Texture Slots Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
            float size = 72.0f;
            float avl_x = ImGui::GetContentRegionAvail().x;
            int cols = (int)(avl_x / (size + 8.0f));
            if (cols < 1) cols = 1;

            for (int i = 0; i < (int)pass.pass_textures.textures.size(); i++) {
                if (i > 0 && i % cols != 0) ImGui::SameLine();
                int shader_slot = i + 1;  // textures[0] = g_Texture1, textures[1] = g_Texture2, ...

                // Resolve label from shader-parsed texture_labels (keyed by g_Texture#)
                const char* slot_label = nullptr;
                if (pass.texture_labels.count(shader_slot)) {
                    slot_label = pass.texture_labels[shader_slot].c_str();
                }

                bool valid = i < (int)pass.pass_textures.cached_views.size() &&
                             pass.pass_textures.cached_views[i].id != SG_INVALID_ID;
                ImVec4 border_color = valid ? ImVec4(0.3f, 0.3f, 0.3f, 1) : ImVec4(1, 0.2f, 0.2f, 1);

                ImGui::BeginGroup();
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                if (valid) {
                    ImGui::Image((ImTextureID)simgui_imtextureid(pass.pass_textures.cached_views[i]),
                                 ImVec2(size, size));
                } else {
                    ImGui::Dummy(ImVec2(size, size));
                }
                ImVec2 p1 = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(p0, p1, ImColor(border_color), 0, 0, valid ? 1.0f : 2.0f);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("g_Texture%d: %s", shader_slot, slot_label ? slot_label : "Extra");
                    if (i < (int)pass.pass_textures.texture_paths.size() &&
                        !pass.pass_textures.texture_paths[i].empty())
                        ImGui::Text("Path: %s", pass.pass_textures.texture_paths[i].c_str());
                    if (!valid) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "INVALID - renders as black");
                    ImGui::EndTooltip();
                }

                ImGui::Text("g_Tex%d", shader_slot);
                if (slot_label)
                    ImGui::TextDisabled("%s", slot_label);
                else
                    ImGui::TextDisabled("-");
                ImGui::EndGroup();
            }
            ImGui::TreePop();
        }

        // List view with paths
        for (int i = 0; i < (int)pass.pass_textures.textures.size(); i++) {
            ImGui::PushID(i);
            int shader_slot = i + 1;

            // Label from shader
            const char* slot_desc = "Extra";
            if (pass.texture_labels.count(shader_slot)) {
                slot_desc = pass.texture_labels[shader_slot].c_str();
            }

            bool valid = i < (int)pass.pass_textures.cached_views.size() &&
                         pass.pass_textures.cached_views[i].id != SG_INVALID_ID;

            if (i < (int)pass.pass_textures.texture_masks.size()) {
                bool m = pass.pass_textures.texture_masks[i];
                if (ImGui::Checkbox("##mask", &m)) pass.pass_textures.texture_masks[i] = m;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle this texture slot");
                ImGui::SameLine();
            }

            if (!valid) {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "g_Texture%d [%s]: INVALID", shader_slot, slot_desc);
            } else if (!pass.pass_textures.texture_paths[i].empty()) {
                const char* full_path = pass.pass_textures.texture_paths[i].c_str();
                const char* filename = strrchr(full_path, '/');
                if (filename)
                    filename++;
                else
                    filename = full_path;
                ImGui::Text("g_Texture%d [%s]: %s", shader_slot, slot_desc, filename);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", full_path);
            } else {
                ImGui::Text("g_Texture%d [%s]: (ok)", shader_slot, slot_desc);
            }
            ImGui::PopID();
        }
    }
    ImGui::Unindent();
    ImGui::PopID();
}

void showEffect(EngineContext& ctx, ::Effect& effect, int id) {
    ImGui::PushID(id);

    // Visibility Toggle
    if (ImGui::Button(effect.visible ? "V" : " ", ImVec2(25, 0))) {
        effect.visible = !effect.visible;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Effect Visibility");

    ImGui::SameLine();

    // Solo Toggle
    if (effect.solo) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
    }
    if (ImGui::Button("S", ImVec2(25, 0))) {
        effect.solo = !effect.solo;
    }
    if (effect.solo) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo Effect (Ctrl+Click effect name also works)");

    ImGui::SameLine();

    std::string effect_name = "Unknown Effect";
    if (!effect.passes.empty() && !effect.passes[0]->shader_name.empty()) {
        effect_name = effect.passes[0]->shader_name;
    } else {
        size_t last_slash = effect.file_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string sub = effect.file_path.substr(0, last_slash);
            size_t prev_slash = sub.find_last_of('/');
            if (prev_slash != std::string::npos) {
                effect_name = sub.substr(prev_slash + 1);
            } else {
                effect_name = sub;
            }
        } else {
            effect_name = effect.file_path;
        }
    }

    if (!effect_name.empty()) {
        effect_name[0] = toupper(effect_name[0]);
    }

    bool open = ImGui::TreeNodeEx(effect_name.c_str(), ImGuiTreeNodeFlags_FramePadding);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s (Ctrl+Click to toggle Solo)", effect.file_path.c_str());
    }
    if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
        effect.solo = !effect.solo;
    }

    if (open) {
        ImGui::Indent();
        for (int i = 0; i < (int)effect.passes.size(); i++) {
            showShaderPass(ctx, *effect.passes[i], i);
        }
        ImGui::Unindent();
        ImGui::TreePop();
    }
    ImGui::PopID();
}

}  // namespace Inspector
