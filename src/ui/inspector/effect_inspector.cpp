#include "effect_inspector.h"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "ui/widgets/visibility_solo_controls.h"
#include "util/sokol_imgui.h"
#include "wallpaper/scene/2d/effects/effect.h"

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
    mode_names.push_back("g_Texture0 [Color]");

    // Resolve labels for dropdown
    for (int i = 0; i < (int)pass.pass_textures.textures.size(); i++) {
        int slot = i + 1;
        char buf[64];
        const char* label = pass.texture_labels.count(slot) ? pass.texture_labels[slot].c_str() : "Extra";
        snprintf(buf, sizeof(buf), "g_Texture%d [%s]", slot, label);
        mode_names.push_back(buf);
    }

    mode_names.push_back("g_Texture0 (Red)");
    for (int i = 0; i < (int)pass.pass_textures.textures.size(); i++) {
        int slot = i + 1;
        char buf[64];
        const char* label = pass.texture_labels.count(slot) ? pass.texture_labels[slot].c_str() : "Extra";
        snprintf(buf, sizeof(buf), "g_Texture%d [%s] (Red)", slot, label);
        mode_names.push_back(buf);
    }

    std::vector<const char*> mode_ptrs;
    for (auto& s : mode_names) mode_ptrs.push_back(s.c_str());

    int prev_mode = pass.debug_view_mode;
    // Map current mode to index in mode_ptrs
    int current_idx = 0;
    int extra_count = (int)pass.pass_textures.textures.size();
    if (pass.debug_view_mode >= 1 && pass.debug_view_mode <= 10) {
        current_idx = pass.debug_view_mode;
    } else if (pass.debug_view_mode >= 11 && pass.debug_view_mode <= 20) {
        current_idx = (pass.debug_view_mode - 11) + (1 + extra_count) + 1;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("##debugview", &current_idx, mode_ptrs.data(), (int)mode_ptrs.size())) {
        if (current_idx == 0)
            pass.debug_view_mode = 0;
        else if (current_idx <= 1 + extra_count)
            pass.debug_view_mode = current_idx;
        else
            pass.debug_view_mode = (current_idx - (1 + extra_count) - 1) + 11;

        if (pass.debug_view_mode != prev_mode) {
            pass.rebuildWithDebugMode(pass.debug_view_mode, ctx);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visual debug: override fragment output to inspect shader values");

    int prev_step = pass.debug_step;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);

    int max_step = 8;
    if (extra_count + 1 > max_step) max_step = extra_count + 1;

    if (ImGui::SliderInt("##debugstep", &pass.debug_step, 0, max_step)) {
        if (pass.debug_step != prev_step) {
            pass.rebuildWithDebugMode(pass.debug_view_mode, ctx);
        }
    }
    if (ImGui::IsItemHovered()) {
        if (pass.shader_name.find("depthparallax") != std::string::npos) {
            ImGui::SetTooltip("0:Full, 1:Color, 2:Depth, 3:Mask, 4:Neutral, 5:Offset, 6:Grayscale, 7:Full(explicit)");
        } else {
            ImGui::SetTooltip("Debug Step: forced texture output for slot N");
        }
    }

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

    UiWidgets::drawVisibilitySoloControls(effect.visible, effect.solo, "Toggle Effect Visibility",
                                          "Solo Effect (Ctrl+Click effect name also works)");

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
