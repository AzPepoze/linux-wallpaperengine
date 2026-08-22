#include "debugger.h"

#if DEBUG_BUILD

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "sandbox_catalog.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "sokol_app.h"
#include "sokol_log.h"
#include "ui/inspector/layer_inspector.h"
#include "ui/widgets/visibility_solo_controls.h"
#include "util/sokol_imgui.h"
#include "wallpaper/2d/layers/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {

SandboxCatalog g_sandbox_catalog;
SandboxProjectLoader g_sandbox_loader = nullptr;
int g_sandbox_tab = 0;
int g_selected_effect = -1;
int g_selected_material = -1;
bool g_logs_open = false;
std::string g_sandbox_status;
SandboxPreviewRect g_sandbox_preview_rect;

int findLayerIndex(const EngineContext& ctx, uint32_t scene_object_id) {
    for (int index = 0; index < (int)ctx.layers.size(); ++index) {
        if (ctx.layers[index]->scene_object_id == scene_object_id) return index;
    }
    return -1;
}

void drawSceneNode(EngineContext& ctx, const SceneTreeNode& node) {
    const int layer_index = findLayerIndex(ctx, node.id);
    const bool is_selected = layer_index >= 0 && ctx.selected_object == layer_index;
    const bool is_leaf = node.children.empty();
    std::string node_name = node.name.empty() ? "Node " + std::to_string(node.id) : node.name;
    if (layer_index >= 0 && layer_index < (int)ctx.layers.size()) {
        const Layer* layer = ctx.layers[layer_index];
        if (const auto* il = dynamic_cast<const ImageLayer*>(layer)) {
            if (il->is_fullscreen)
                node_name += " [FS PostProcess]";
            else if (il->solid_layer)
                node_name += " [Solid]";
            else
                node_name += " [Image]";

            if (il->color_blend_mode != 0) {
                node_name += " (Blend " + std::to_string(il->color_blend_mode) + ")";
            }
        } else if (dynamic_cast<const ParticleLayer*>(layer)) {
            node_name += " [Particle]";
        }
        if (node.parallax_depth[0] != 0.0f || node.parallax_depth[1] != 0.0f) {
            char p_buf[64];
            snprintf(p_buf, sizeof(p_buf), " [P:%.1f,%.1f]", node.parallax_depth[0], node.parallax_depth[1]);
            node_name += p_buf;
        }
    }
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (is_leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID((int)node.id);
    if (layer_index >= 0) {
        UiWidgets::drawVisibilitySoloControls(*ctx.layers[layer_index], "Toggle layer visibility", "Solo layer");
        ImGui::SameLine();
    }
    const bool open = ImGui::TreeNodeEx(node_name.c_str(), flags);
    if (layer_index >= 0 && ImGui::IsItemClicked()) {
        ctx.selected_object = layer_index;
        if (ImGui::GetIO().KeyCtrl) ctx.layers[layer_index]->setSolo(!ctx.layers[layer_index]->solo);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scene node %u", node.id);

    if (open && !is_leaf) {
        for (uint32_t child_id : node.children) {
            const SceneTreeNode* child = ctx.scene_tree->find(child_id);
            if (child) drawSceneNode(ctx, *child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void drawHierarchyPanel(EngineContext& ctx) {
    const double fps = ctx.profiler.frame_avg_ms > 0.0 ? (1000.0 / ctx.profiler.frame_avg_ms) : 0.0;
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "SCENE TREE");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.1f FPS)", fps);
    ImGui::Separator();

    const float isolate_btn_w = 72.0f;
    const float available_w = ImGui::GetContentRegionAvail().x;
    const float selectable_w = available_w - isolate_btn_w - ImGui::GetStyle().ItemSpacing.x;

    if (ImGui::Selectable("Global Settings", ctx.selected_object == -1, 0,
                          ImVec2(selectable_w > 40.0f ? selectable_w : 0.0f, 0))) {
        ctx.selected_object = -1;
    }
    ImGui::SameLine();
    if (ctx.test_mode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
    }
    if (ImGui::Button(ctx.test_mode ? "Isolate: ON" : "Isolate", ImVec2(isolate_btn_w, 0))) {
        ctx.test_mode = !ctx.test_mode;
    }
    if (ctx.test_mode) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Render only the selected layer");

    ImGui::Separator();
    if (ctx.scene_tree && ctx.scene_tree->size() > 0) {
        for (uint32_t root_id : ctx.scene_tree->rootIds()) {
            const SceneTreeNode* root = ctx.scene_tree->find(root_id);
            if (root) drawSceneNode(ctx, *root);
        }
        return;
    }

    for (int index = 0; index < (int)ctx.layers.size(); ++index) {
        Layer* layer = ctx.layers[index];
        ImGui::PushID(index);
        UiWidgets::drawVisibilitySoloControls(*layer, "Toggle layer visibility", "Solo layer");
        ImGui::SameLine();
        if (ImGui::Selectable(layer->name.c_str(), ctx.selected_object == index)) ctx.selected_object = index;
        ImGui::PopID();
    }
}

void drawInspectorPanel(EngineContext& ctx) {
    const double fps = ctx.profiler.frame_avg_ms > 0.0 ? (1000.0 / ctx.profiler.frame_avg_ms) : 0.0;
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "INSPECTOR");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[ %.1f FPS | %.1f ms ]", fps, ctx.profiler.frame_avg_ms);
    ImGui::Separator();

    if (ctx.selected_object == -1) {
        Inspector::showGlobalSettings(ctx);
        return;
    }

    if (ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        Layer* layer = ctx.layers[ctx.selected_object];
        ImGui::Text("Selected: %s", layer->name.c_str());
        ImGui::Separator();
        Inspector::showLayer(ctx, *layer);
    }
}

void drawSandboxEntries(const std::vector<SandboxEntry>& entries, int& selected_index) {
    if (entries.empty()) {
        ImGui::TextDisabled("No installed entries found.");
        return;
    }

    for (int index = 0; index < (int)entries.size(); ++index) {
        const SandboxEntry& entry = entries[index];
        ImGui::PushID(index);
        const std::string label = entry.available ? entry.name : entry.name + " (unavailable)";
        const bool selected = selected_index == index;
        if (ImGui::Selectable(label.c_str(), selected)) {
            selected_index = index;
            if (entry.available && g_sandbox_loader && g_sandbox_loader(entry.preview_project_path.c_str())) {
                g_sandbox_status = "Loaded " + entry.preview_project_path;
            } else if (!entry.available) {
                g_sandbox_status = entry.name + " has no bundled preview/scene.json project.";
            } else {
                g_sandbox_status = "Could not load " + entry.preview_project_path;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", entry.available ? entry.preview_project_path.c_str() : entry.source_path.c_str());
        }
        ImGui::PopID();
    }
}

void selectFirstPreview(EngineContext& ctx) {
    const auto& effects = g_sandbox_catalog.effects();
    const auto first_available =
        std::find_if(effects.begin(), effects.end(), [](const SandboxEntry& entry) { return entry.available; });
    if (first_available == effects.end() || !g_sandbox_loader) {
        g_sandbox_status = "No installed effect includes a preview/scene.json project.";
        return;
    }

    g_selected_effect = (int)std::distance(effects.begin(), first_available);
    if (g_sandbox_loader(first_available->preview_project_path.c_str())) {
        g_sandbox_status = "Loaded " + first_available->preview_project_path;
    } else {
        g_sandbox_status = "Could not load " + first_available->preview_project_path;
    }
    (void)ctx;
}

}  // namespace

void Debugger::init() {
    simgui_desc_t desc = {};
    desc.logger.func = slog_func;
    simgui_setup(&desc);
}

void Debugger::startSandbox(EngineContext& ctx, SandboxProjectLoader loader) {
    g_sandbox_loader = loader;
    g_sandbox_catalog.scan(ctx.engine_path);
    g_selected_effect = -1;
    g_selected_material = -1;
    selectFirstPreview(ctx);
}

SandboxPreviewRect Debugger::sandboxPreviewRect() {
    return g_sandbox_preview_rect;
}

void Debugger::drawSceneTab(EngineContext& ctx) {
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    constexpr float button_height = 32.0f;
    const float panel_height = ImGui::GetContentRegionAvail().y - button_height - ImGui::GetStyle().ItemSpacing.y;
    if (!ImGui::BeginTable("SceneLayout", 3, table_flags, ImVec2(0.0f, panel_height))) return;

    ImGui::TableSetupColumn("Scene Tree", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.25f);
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.03f, 0.05f, 0.92f));
    ImGui::BeginChild("SceneTree", ImVec2(0.0f, 0.0f), true);
    drawHierarchyPanel(ctx);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::TableNextColumn();
    ImGui::Dummy(ImGui::GetContentRegionAvail());

    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.03f, 0.05f, 0.92f));
    ImGui::BeginChild("SceneInspector", ImVec2(0.0f, 0.0f), true);
    if (ImGui::BeginTabBar("RightPanelTabs")) {
        if (ImGui::BeginTabItem("Inspector")) {
            drawInspectorPanel(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics")) {
            drawDiagnosticsTab(ctx);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::EndTable();

    if (!g_logs_open) {
        constexpr float button_width = 160.0f;
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowSize().x - button_width) * 0.5f,
                                   ImGui::GetWindowSize().y - button_height - ImGui::GetStyle().WindowPadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.30f, 0.58f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.42f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.22f, 0.45f, 1.0f));
        if (ImGui::Button("Open Logs", ImVec2(button_width, button_height))) g_logs_open = true;
        ImGui::PopStyleColor(3);
    }

    if (ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->drawDebug(ctx);
    }
}

void Debugger::drawSandbox(EngineContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)sapp_width(), (float)sapp_height()), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Sandbox", nullptr, flags);

    const char* tabs[] = {"Effects", "Materials", "Logs"};
    for (int tab = 0; tab < IM_ARRAYSIZE(tabs); ++tab) {
        if (tab > 0) ImGui::SameLine();
        if (ImGui::Button(tabs[tab], ImVec2(100.0f, 0.0f))) g_sandbox_tab = tab;
    }
    ImGui::Separator();

    if (g_sandbox_tab == 2) {
        drawLogsTab();
        ImGui::End();
        return;
    }

    ImGui::BeginChild("SandboxEntries", ImVec2(360.0f, 0.0f), true);
    if (g_sandbox_tab == 0) {
        ImGui::Text("Installed effect previews");
        ImGui::Separator();
        drawSandboxEntries(g_sandbox_catalog.effects(), g_selected_effect);
    } else {
        ImGui::Text("Installed material definitions");
        ImGui::TextDisabled("Each available material uses its effect's supplied preview project.");
        ImGui::Separator();
        drawSandboxEntries(g_sandbox_catalog.materials(), g_selected_material);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("SandboxPreview", ImVec2(0.0f, 0.0f), true);
    const ImVec2 preview_position = ImGui::GetWindowPos();
    const ImVec2 preview_size = ImGui::GetWindowSize();
    const float dpi_scale = sapp_dpi_scale();
    g_sandbox_preview_rect = {
        static_cast<int>(preview_position.x * dpi_scale), static_cast<int>(preview_position.y * dpi_scale),
        static_cast<int>(preview_size.x * dpi_scale), static_cast<int>(preview_size.y * dpi_scale)};
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "RENDERED PREVIEW");
    ImGui::Separator();
    ImGui::TextWrapped("The selected Wallpaper Engine preview project is rendered in this fixed preview area.");
    if (!g_sandbox_status.empty()) ImGui::TextWrapped("%s", g_sandbox_status.c_str());
    ImGui::TextDisabled("Engine: %s", ctx.engine_path);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

void Debugger::draw(EngineContext& ctx) {
    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = sapp_width();
    frame_desc.height = sapp_height();
    frame_desc.delta_time = (float)sapp_frame_duration();
    frame_desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&frame_desc);

    if (ctx.runtime_mode == RuntimeMode::Sandbox) {
        drawSandbox(ctx);
    } else if (ctx.show_ui) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)sapp_width(), (float)sapp_height()), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("DebugWorkspace", nullptr, flags);
        drawSceneTab(ctx);
        ImGui::End();

        if (g_logs_open) {
            const float max_width = std::max(360.0f, (float)sapp_width() - 32.0f);
            const float max_height = std::max(240.0f, (float)sapp_height() - 32.0f);
            const ImVec2 log_window_size(std::min(720.0f, max_width), std::min(420.0f, max_height));
            ImGui::SetNextWindowPos(ImVec2(((float)sapp_width() - log_window_size.x) * 0.5f,
                                           ((float)sapp_height() - log_window_size.y) * 0.5f),
                                    ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(log_window_size, ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 240.0f), ImVec2(max_width, max_height));
            if (ImGui::Begin("Logs", &g_logs_open)) {
                drawLogsTab();
            }
            ImGui::End();
        }
    }
    simgui_render();
}

#endif  // DEBUG_BUILD
