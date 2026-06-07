#include "debugger.h"

#include <stdio.h>

#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_imgui.h"
#include "../../libs/sokol/sokol_log.h"
#include "../core/context.h"
#include "imgui.h"

void debugger_init(void) {
    simgui_desc_t desc = {};
    desc.logger.func = slog_func;
    simgui_setup(&desc);
}

void debugger_draw(void) {
    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = sapp_width();
    frame_desc.height = sapp_height();
    frame_desc.delta_time = (float)sapp_frame_duration();
    frame_desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&frame_desc);

    if (state.show_ui) {
        if (ImGui::Begin("Debugger", &state.show_ui)) {
            if (ImGui::CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* modes[] = {"Cover", "Fit"};
                int current_mode = (int)state.scaling_mode;
                if (ImGui::Combo("Scaling Mode", &current_mode, modes, 2))
                    state.scaling_mode = (scaling_mode_t)current_mode;
                ImGui::Text("Resolution: %.0fx%.0f", state.scene_w, state.scene_h);
                ImGui::Text("Render Scale: %.3f", state.render_scale);
                ImGui::Text("Layers: %d", (int)state.layers.size());
            }
            if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < (int)state.layers.size(); i++) {
                    char label[160];
                    snprintf(label, sizeof(label), "%d: %s", i, state.layers[i]->name.c_str());
                    if (ImGui::Selectable(label, state.selected_object == i)) state.selected_object = i;
                }
            }
            if (state.selected_object >= 0 && state.selected_object < (int)state.layers.size()) {
                Layer* layer = state.layers[state.selected_object];
                if (ImGui::CollapsingHeader("Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
                    layer->showInspector();
                }
            }
        }
        ImGui::End();
    }
    simgui_render();
}
