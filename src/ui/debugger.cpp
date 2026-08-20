#include "debugger.h"

#include <stdio.h>

#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_imgui.h"
#include "../../libs/sokol/sokol_log.h"
#include "../core/context.h"
#include "../core/logger.h"
#include "imgui.h"
#include "inspector/layer_inspector.h"
#include "wallpaper/scene/2d/effects/effect.h"
#include "wallpaper/scene/2d/layers/layer.h"

namespace {
bool g_log_debug_ui_layout = true;
int g_last_surface_width = -1;
int g_last_surface_height = -1;
float g_last_dpi_scale = -1.0f;
}  // namespace

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

    bool visible = ImGui::Begin("HierarchyPanel", nullptr, flags);
    if (g_log_debug_ui_layout) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        LOG_TAG_D("DEBUG_UI", "HierarchyPanel: visible=%d requested=(0.0,0.0 %.1fx%.1f) actual=(%.1f,%.1f %.1fx%.1f)",
                  visible ? 1 : 0, width, height, pos.x, pos.y, size.x, size.y);
    }

    if (visible) {
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
        if (ImGui::Button("Isolate", ImVec2(80, 0))) ctx.test_mode = !ctx.test_mode;
        if (is_test) ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show ONLY the selected layer and its effects");

        ImGui::Separator();
        ImGui::BeginChild("LayerList");
        for (int i = 0; i < (int)ctx.layers.size(); i++) {
            Layer* layer = ctx.layers[i];
            ImGui::PushID(i);

            if (ImGui::Button(layer->visible ? "V" : " ", ImVec2(25, 0))) layer->visible = !layer->visible;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Visibility");

            ImGui::SameLine();
            if (layer->solo) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
            }
            if (ImGui::Button("S", ImVec2(25, 0))) layer->solo = !layer->solo;
            if (layer->solo) ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo Layer (Ctrl+Click layer name also works)");

            ImGui::SameLine();
            char label[160];
            snprintf(label, sizeof(label), "%02d: %s", i, layer->name.c_str());
            if (ImGui::Selectable(label, ctx.selected_object == i)) {
                ctx.selected_object = i;
                if (ImGui::GetIO().KeyCtrl) layer->solo = !layer->solo;
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

    bool visible = ImGui::Begin("InspectorPanel", nullptr, flags);
    if (g_log_debug_ui_layout) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        LOG_TAG_D("DEBUG_UI", "InspectorPanel: visible=%d requested=(%.1f,0.0 %.1fx%.1f) actual=(%.1f,%.1f %.1fx%.1f)",
                  visible ? 1 : 0, x_pos, current_width, height, pos.x, pos.y, size.x, size.y);
    }

    if (visible) {
        current_width = ImGui::GetWindowWidth();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "INSPECTOR");
        ImGui::Separator();

        if (ctx.selected_object == -1) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "GLOBAL ENGINE SETTINGS");
            ImGui::Separator();
            const char* modes[] = {"Cover", "Fit"};
            int current_mode = (int)ctx.scaling_mode;
            if (ImGui::Combo("Scaling Mode", &current_mode, modes, 2)) ctx.scaling_mode = (scaling_mode_t)current_mode;
            ImGui::Text("Resolution: %.0fx%.0f", ctx.scene_w, ctx.scene_h);
            ImGui::Text("Render Scale: %.3f", ctx.render_scale);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

            ImGui::Separator();
            if (ImGui::TreeNodeEx("PERFORMANCE", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("CPU Frame: %.3f ms", ctx.profiler.frame_ms);
                ImGui::Text("Rolling Avg: %.3f ms", ctx.profiler.frame_avg_ms);
                ImGui::Text("Peak: %.3f ms", ctx.profiler.frame_peak_ms);
                ImGui::Separator();
                ImGui::Text("Update: %.3f ms", ctx.profiler.update_ms);
                ImGui::Text("Render: %.3f ms", ctx.profiler.render_ms);
                ImGui::Text("Debug UI: %.3f ms", ctx.profiler.ui_ms);
                ImGui::Text("Draw Calls: %u", ctx.profiler.draw_calls);
                ImGui::Text("Frame: %llu", (unsigned long long)ctx.profiler.frame_index);
                if (ImGui::Button("Reset Peak")) ctx.profiler.frame_peak_ms = ctx.profiler.frame_ms;
                ImGui::TreePop();
            }

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
    const int surface_width = sapp_width();
    const int surface_height = sapp_height();
    const float dpi_scale = sapp_dpi_scale();

    if (surface_width != g_last_surface_width || surface_height != g_last_surface_height ||
        dpi_scale != g_last_dpi_scale) {
        g_log_debug_ui_layout = true;
        g_last_surface_width = surface_width;
        g_last_surface_height = surface_height;
        g_last_dpi_scale = dpi_scale;
    }

    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = surface_width;
    frame_desc.height = surface_height;
    frame_desc.delta_time = (float)sapp_frame_duration();
    frame_desc.dpi_scale = dpi_scale;
    simgui_new_frame(&frame_desc);

    if (g_log_debug_ui_layout) {
        const ImGuiIO& io = ImGui::GetIO();
        LOG_TAG_D("DEBUG_UI",
                  "Frame setup: show_ui=%d surface=%dx%d dpi=%.3f display=%.1fx%.1f framebuffer_scale=%.3fx%.3f",
                  ctx.show_ui ? 1 : 0, surface_width, surface_height, dpi_scale, io.DisplaySize.x, io.DisplaySize.y,
                  io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    }

    if (ctx.show_ui) {
        float panel_width = 300.0f;
        float screen_height = (float)surface_height;
        drawHierarchyPanel(ctx, panel_width, screen_height);
        drawInspectorPanel(ctx, panel_width, screen_height);

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

    if (g_log_debug_ui_layout) {
        const ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data) {
            LOG_TAG_D("DEBUG_UI",
                      "Draw data: valid=%d cmd_lists=%d vertices=%d indices=%d display_pos=(%.1f,%.1f) "
                      "display_size=(%.1f,%.1f) framebuffer_scale=(%.3f,%.3f)",
                      draw_data->Valid ? 1 : 0, draw_data->CmdListsCount, draw_data->TotalVtxCount,
                      draw_data->TotalIdxCount, draw_data->DisplayPos.x, draw_data->DisplayPos.y,
                      draw_data->DisplaySize.x, draw_data->DisplaySize.y, draw_data->FramebufferScale.x,
                      draw_data->FramebufferScale.y);
        } else {
            LOG_TAG_W("DEBUG_UI", "Draw data is null after simgui_render()");
        }
        g_log_debug_ui_layout = false;
    }
}
