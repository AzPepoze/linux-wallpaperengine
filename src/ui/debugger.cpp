#include "debugger.h"

#include <stdio.h>

#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_imgui.h"
#include "../../libs/sokol/sokol_log.h"
#include "../core/context.h"
#include "imgui.h"

sg_image Debugger::preview_texture = {SG_INVALID_ID};
sg_view Debugger::preview_view = {SG_INVALID_ID};
float Debugger::preview_aspect = 1.0f;

void Debugger::init() {
    simgui_desc_t desc = {};
    desc.logger.func = slog_func;
    simgui_setup(&desc);
}

void Debugger::setPreviewTexture(sg_image img, float aspect) {
    if (preview_view.id != SG_INVALID_ID) {
        sg_destroy_view(preview_view);
        preview_view.id = SG_INVALID_ID;
    }
    preview_texture = img;
    preview_aspect = aspect;
    if (img.id != SG_INVALID_ID) {
        sg_view_desc desc = {};
        desc.texture.image = img;
        preview_view = sg_make_view(&desc);
    }
}

void Debugger::drawHierarchyPanel(float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(100, height), ImVec2(sapp_width() * 0.5f, height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("HierarchyPanel", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "HIERARCHY");
        ImGui::Separator();

        float avail_w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("Global Settings", state.selected_object == -1, 0, ImVec2(avail_w - 90, 0))) {
            state.selected_object = -1;
        }

        ImGui::SameLine();
        bool is_test = state.test_mode;
        if (is_test) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.3f, 0.0f, 1.0f));
        }
        if (ImGui::Button("Isolate", ImVec2(80, 0))) {
            state.test_mode = !state.test_mode;
        }
        if (is_test) ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show ONLY the selected layer and its effects");

        ImGui::Separator();
        ImGui::BeginChild("LayerList");
        for (int i = 0; i < (int)state.layers.size(); i++) {
            Layer* layer = state.layers[i];
            ImGui::PushID(i);

            // Visibility Toggle
            if (ImGui::Button(layer->visible ? "V" : " ", ImVec2(25, 0))) {
                layer->visible = !layer->visible;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Visibility");

            ImGui::SameLine();

            // Solo Toggle
            if (layer->solo) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
            }
            if (ImGui::Button("S", ImVec2(25, 0))) {
                layer->solo = !layer->solo;
            }
            if (layer->solo) ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo Layer (Ctrl+Click layer name also works)");

            ImGui::SameLine();

            char label[160];
            snprintf(label, sizeof(label), "%02d: %s", i, layer->name.c_str());
            if (ImGui::Selectable(label, state.selected_object == i)) {
                state.selected_object = i;
                if (ImGui::GetIO().KeyCtrl) {
                    layer->solo = !layer->solo;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select layer. Ctrl+Click to toggle Solo.");

            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void Debugger::drawInspectorPanel(float width, float height) {
    static float current_width = width;
    float x_pos = (float)sapp_width() - current_width;
    ImGui::SetNextWindowPos(ImVec2(x_pos, 0));
    ImGui::SetNextWindowSize(ImVec2(current_width, height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(100, height), ImVec2(sapp_width() * 0.5f, height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("InspectorPanel", nullptr, flags)) {
        current_width = ImGui::GetWindowWidth();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "INSPECTOR");
        ImGui::Separator();

        if (state.selected_object == -1) {
            ImGui::Text("Global Engine Settings");
            ImGui::Separator();
            const char* modes[] = {"Cover", "Fit"};
            int current_mode = (int)state.scaling_mode;
            if (ImGui::Combo("Scaling Mode", &current_mode, modes, 2)) {
                state.scaling_mode = (scaling_mode_t)current_mode;
            }
            ImGui::Text("Resolution: %.0fx%.0f", state.scene_w, state.scene_h);
            ImGui::Text("Render Scale: %.3f", state.render_scale);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        } else if (state.selected_object >= 0 && state.selected_object < (int)state.layers.size()) {
            Layer* layer = state.layers[state.selected_object];
            ImGui::Text("Selected: %s", layer->name.c_str());
            ImGui::Separator();
            layer->showInspector();
        }
    }
    ImGui::End();
}

void Debugger::draw() {
    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = sapp_width();
    frame_desc.height = sapp_height();
    frame_desc.delta_time = (float)sapp_frame_duration();
    frame_desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&frame_desc);

    if (state.show_ui) {
        float panel_width = 300.0f;
        float screen_height = (float)sapp_height();

        drawHierarchyPanel(panel_width, screen_height);
        drawInspectorPanel(panel_width, screen_height);

        // Draw debug bounds for selected layer
        if (state.selected_object >= 0 && state.selected_object < (int)state.layers.size()) {
            state.layers[state.selected_object]->drawDebug();
        }

        if (preview_view.id != SG_INVALID_ID) {
            ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
            bool open = true;
            if (ImGui::Begin("Texture Preview", &open)) {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float w = avail.x;
                float h = w / preview_aspect;
                if (h > avail.y) {
                    h = avail.y;
                    w = h * preview_aspect;
                }
                ImGui::Image((ImTextureID)simgui_imtextureid(preview_view), ImVec2(w, h));
            }
            ImGui::End();
            if (!open) setPreviewTexture({SG_INVALID_ID}, 1.0f);
        }
    }
    simgui_render();
}
