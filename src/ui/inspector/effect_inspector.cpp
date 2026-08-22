#include "effect_inspector.h"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "ui/widgets/visibility_solo_controls.h"
#include "util/sokol_imgui.h"
#include "wallpaper/2d/effects/effect.h"

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
    // g_Texture0 is a real resolved slot too: it may be an authored image,
    // the previous pass, or an invalid fallback.  Keep it visible alongside
    // the numbered texture array rather than treating it as implicit.
    if (ImGui::TreeNodeEx("Resolved Texture Slots", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto show_resolved_slot = [&](int slot, sg_image image, sg_view view, const std::string& path,
                                      const char* source) {
            const char* label = pass.texture_labels.count(slot) ? pass.texture_labels[slot].c_str() : "Texture";
            const bool valid = view.id != SG_INVALID_ID;
            ImGui::Text("g_Texture%d [%s] — %s", slot, label, valid ? "bound" : "INVALID / fallback");
            ImGui::SameLine();
            ImGui::TextDisabled("source: %s", source);
            if (valid) {
                const sg_image_desc desc = sg_query_image_desc(image);
                ImGui::Image((ImTextureID)simgui_imtextureid(view), ImVec2(96.0f, 64.0f));
                ImGui::SameLine();
                ImGui::TextDisabled("%d x %d%s%s", desc.width, desc.height, path.empty() ? "" : "\n",
                                    path.empty() ? "" : path.c_str());
            } else if (!path.empty()) {
                ImGui::TextDisabled("%s", path.c_str());
            }
        };
        show_resolved_slot(
            0, pass.pass_textures.texture0, pass.pass_textures.texture0_view, pass.pass_textures.texture0_path,
            pass.render_texture_bindings.count(0) ? pass.render_texture_bindings[0].c_str() : "previous/authored");
        for (int i = 0; i < (int)pass.pass_textures.textures.size(); ++i) {
            const int slot = i + 1;
            const char* source =
                pass.render_texture_bindings.count(slot) ? pass.render_texture_bindings[slot].c_str() : "authored";
            sg_view view = {SG_INVALID_ID};
            if (i < (int)pass.pass_textures.cached_views.size()) view = pass.pass_textures.cached_views[i];
            show_resolved_slot(
                slot, pass.pass_textures.textures[i], view,
                i < (int)pass.pass_textures.texture_paths.size() ? pass.pass_textures.texture_paths[i] : std::string(),
                source);
        }
        ImGui::TreePop();
    }

    if (!pass.pass_textures.textures.empty()) {
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

    if (!pass.uniforms.empty() && ImGui::TreeNodeEx("Shader Uniforms", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& [name, values] : pass.uniforms) {
            if (values.empty()) continue;
            ImGui::PushID(name.c_str());
            if (values.size() == 1) {
                ImGui::DragFloat(name.c_str(), &values[0], 0.01f);
            } else if (values.size() == 2) {
                ImGui::DragFloat2(name.c_str(), values.data(), 0.01f);
            } else if (values.size() == 3) {
                ImGui::DragFloat3(name.c_str(), values.data(), 0.01f);
            } else if (values.size() >= 4) {
                if (name.find("color") != std::string::npos || name.find("Color") != std::string::npos ||
                    name.find("tint") != std::string::npos || name.find("Tint") != std::string::npos) {
                    ImGui::ColorEdit4(name.c_str(), values.data());
                } else {
                    ImGui::DragFloat4(name.c_str(), values.data(), 0.01f);
                }
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (!pass.combos.empty() && ImGui::TreeNode("Shader Combos")) {
        for (const auto& combo : pass.combos) ImGui::BulletText("%s = %d", combo.first.c_str(), combo.second);
        ImGui::TreePop();
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
