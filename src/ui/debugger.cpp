#include "debugger.h"

#include <stdio.h>

#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_imgui.h"
#include "../../libs/sokol/sokol_log.h"
#include "../core/context.h"
#include "../render/effect.h"
#include "../scene/layer.h"
#include "imgui.h"
#include "inspector/layer_inspector.h"

GfxImage Debugger::preview_texture;
GfxView Debugger::preview_view;
float Debugger::preview_aspect = 1.0f;

void Debugger::init() {
    simgui_desc_t desc = {};
    desc.logger.func = slog_func;
    simgui_setup(&desc);
}

void Debugger::setPreviewTexture(sg_image img, float aspect) {
    preview_texture = img;
    preview_aspect = aspect;
    if (img.id != SG_INVALID_ID) {
        sg_view_desc desc = {};
        desc.texture.image = img;
        preview_view = sg_make_view(&desc);
    } else {
        preview_view = {};
    }
}

void Debugger::drawHierarchyPanel(EngineContext& ctx, float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(100, height), ImVec2(sapp_width() * 0.5f, height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("HierarchyPanel", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "HIERARCHY");
        ImGui::Separator();

        float avail_w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("Global Settings", ctx.selected_object == -1, 0, ImVec2(avail_w - 90, 0))) {
            ctx.selected_object = -1;
        }

        ImGui::SameLine();
        bool is_test = ctx.test_mode;
        if (is_test) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.3f, 0.0f, 1.0f));
        }
        if (ImGui::Button("Isolate", ImVec2(80, 0))) {
            ctx.test_mode = !ctx.test_mode;
        }
        if (is_test) ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show ONLY the selected layer and its effects");

        ImGui::Separator();
        ImGui::BeginChild("LayerList");
        for (int i = 0; i < (int)ctx.layers.size(); i++) {
            Layer* layer = ctx.layers[i];
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
            if (ImGui::Selectable(label, ctx.selected_object == i)) {
                ctx.selected_object = i;
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

void Debugger::drawInspectorPanel(EngineContext& ctx, float width, float height) {
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

        if (ctx.selected_object == -1) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "GLOBAL ENGINE SETTINGS");
            ImGui::Separator();
            const char* modes[] = {"Cover", "Fit"};
            int current_mode = (int)ctx.scaling_mode;
            if (ImGui::Combo("Scaling Mode", &current_mode, modes, 2)) {
                ctx.scaling_mode = (scaling_mode_t)current_mode;
            }
            ImGui::Text("Resolution: %.0fx%.0f", ctx.scene_w, ctx.scene_h);
            ImGui::Text("Render Scale: %.3f", ctx.render_scale);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

            ImGui::Separator();
            if (ImGui::TreeNodeEx("MASTER EFFECT LIST", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < (int)ctx.layers.size(); i++) {
                    Layer* layer = ctx.layers[i];
                    if (layer->effects.empty()) continue;

                    ImGui::PushID(i);
                    if (ImGui::TreeNode(layer->name.c_str())) {
                        for (int j = 0; j < (int)layer->effects.size(); j++) {
                            Effect* eff = layer->effects[j];
                            ImGui::PushID(j);
                            ImGui::Checkbox(eff->passes.empty() ? "Effect" : eff->passes[0]->shader_name.c_str(),
                                            &eff->visible);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        } else if (ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
            Layer* layer = ctx.layers[ctx.selected_object];
            ImGui::Text("Selected: %s", layer->name.c_str());
            ImGui::Separator();
            Inspector::showLayer(ctx, *layer);
        }
    }
    ImGui::End();
}

void Debugger::draw(EngineContext& ctx) {
    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = sapp_width();
    frame_desc.height = sapp_height();
    frame_desc.delta_time = (float)sapp_frame_duration();
    frame_desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&frame_desc);

    if (ctx.show_ui) {
        float panel_width = 300.0f;
        float screen_height = (float)sapp_height();

        drawHierarchyPanel(ctx, panel_width, screen_height);
        drawInspectorPanel(ctx, panel_width, screen_height);

        // Draw debug bounds for selected layer
        if (ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
            ctx.layers[ctx.selected_object]->drawDebug(ctx);
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
