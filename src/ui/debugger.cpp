#include "debugger.h"

#if DEBUG_BUILD

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/engine_context.h"
#include "core/logger.h"
#include "imgui.h"
#include "render/diagnostics/render_diagnostics.h"
#include "sandbox_catalog.h"
#include "sokol_app.h"
#include "sokol_log.h"
#include "ui/inspector/layer_inspector.h"
#include "util/sokol_imgui.h"
#include "wallpaper/scene/2d/layers/layer.h"
#include "wallpaper/scene/tree/scene_tree.h"

namespace {

SandboxCatalog g_sandbox_catalog;
SandboxProjectLoader g_sandbox_loader = nullptr;
int g_sandbox_tab = 0;
int g_selected_effect = -1;
int g_selected_material = -1;
std::string g_sandbox_status;

int findLayerIndex(const EngineContext& ctx, uint32_t scene_object_id) {
    for (int index = 0; index < (int)ctx.layers.size(); ++index) {
        if (ctx.layers[index]->scene_object_id == scene_object_id) return index;
    }
    return -1;
}

void drawLayerActions(Layer& layer) {
    if (ImGui::SmallButton(layer.visible ? "V" : " ")) layer.visible = !layer.visible;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle visibility");

    ImGui::SameLine();
    if (layer.solo) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.62f, 0.10f, 1.0f));
    if (ImGui::SmallButton("S")) layer.solo = !layer.solo;
    if (layer.solo) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo layer");
}

void drawSceneNode(EngineContext& ctx, const SceneTreeNode& node) {
    const int layer_index = findLayerIndex(ctx, node.id);
    const bool is_selected = layer_index >= 0 && ctx.selected_object == layer_index;
    const bool is_leaf = node.children.empty();
    const std::string node_name = node.name.empty() ? "Node " + std::to_string(node.id) : node.name;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (is_leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID((int)node.id);
    const bool open = ImGui::TreeNodeEx(node_name.c_str(), flags);
    if (layer_index >= 0 && ImGui::IsItemClicked()) {
        ctx.selected_object = layer_index;
        if (ImGui::GetIO().KeyCtrl) ctx.layers[layer_index]->solo = !ctx.layers[layer_index]->solo;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scene node %u", node.id);

    if (layer_index >= 0) {
        ImGui::SameLine();
        drawLayerActions(*ctx.layers[layer_index]);
    }

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
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "SCENE TREE");
    ImGui::Separator();

    if (ImGui::Selectable("Global Settings", ctx.selected_object == -1)) ctx.selected_object = -1;
    ImGui::SameLine();
    if (ImGui::Button(ctx.test_mode ? "Isolate: on" : "Isolate")) ctx.test_mode = !ctx.test_mode;
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
        if (ImGui::Selectable(layer->name.c_str(), ctx.selected_object == index)) ctx.selected_object = index;
        ImGui::SameLine();
        drawLayerActions(*layer);
        ImGui::PopID();
    }
}

void drawInspectorPanel(EngineContext& ctx) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "INSPECTOR");
    ImGui::Separator();

    if (ctx.selected_object == -1) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "GLOBAL ENGINE SETTINGS");
        const char* modes[] = {"Cover", "Fit"};
        int current_mode = (int)ctx.scaling_mode;
        if (ImGui::Combo("Scaling Mode", &current_mode, modes, IM_ARRAYSIZE(modes))) {
            ctx.scaling_mode = (scaling_mode_t)current_mode;
        }
        ImGui::Text("Resolution: %.0fx%.0f", ctx.scene_w, ctx.scene_h);
        ImGui::Text("Render Scale: %.3f", ctx.render_scale);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
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

void Debugger::drawSceneTab(EngineContext& ctx) {
    const float left_width = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.40f);
    ImGui::BeginChild("SceneTree", ImVec2(left_width, 0.0f), true);
    drawHierarchyPanel(ctx);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("SceneInspector", ImVec2(0.0f, 0.0f), true);
    drawInspectorPanel(ctx);
    ImGui::EndChild();

    if (ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->drawDebug(ctx);
    }
}

void Debugger::drawDiagnosticsTab(EngineContext& ctx) {
    RenderDiagnostics& diagnostics = RenderDiagnostics::instance();

    ImGui::Text("Performance");
    ImGui::Separator();
    ImGui::Text("CPU frame: %.3f ms", ctx.profiler.frame_ms);
    ImGui::Text("Rolling average: %.3f ms", ctx.profiler.frame_avg_ms);
    ImGui::Text("Peak: %.3f ms", ctx.profiler.frame_peak_ms);
    ImGui::Text("Update: %.3f ms  Render: %.3f ms  UI: %.3f ms", ctx.profiler.update_ms, ctx.profiler.render_ms,
                ctx.profiler.ui_ms);
    ImGui::Text("Draw calls: %u  Frame: %llu", ctx.profiler.draw_calls, (unsigned long long)ctx.profiler.frame_index);
    if (ImGui::Button("Reset peak")) ctx.profiler.frame_peak_ms = ctx.profiler.frame_ms;

    ImGui::Spacing();
    ImGui::Text("Render capture");
    ImGui::Separator();
    ImGui::Checkbox("Enable diagnostic capture", &diagnostics.config.enabled);
    int capture_frame = (int)diagnostics.config.target_frame;
    if (ImGui::InputInt("Capture frame", &capture_frame, 1, 60)) {
        diagnostics.config.target_frame = (uint64_t)std::max(0, capture_frame);
        diagnostics.config.capture_complete = false;
    }
    char output_dir[512] = {};
    std::strncpy(output_dir, diagnostics.config.output_dir.c_str(), sizeof(output_dir) - 1);
    if (ImGui::InputText("Output directory", output_dir, sizeof(output_dir)))
        diagnostics.config.output_dir = output_dir;
    ImGui::Text("Capture state: %s", diagnostics.is_capturing_frame ? "capturing" : "idle");

    ImGui::Spacing();
    ImGui::Text("Pass isolation");
    ImGui::Separator();
    ImGui::InputInt("Effect index", &diagnostics.config.isolate_effect_index);
    ImGui::InputInt("Pass index", &diagnostics.config.isolate_pass_index);
    ImGui::InputInt("Stop after pass", &diagnostics.config.stop_after_pass_index);
    ImGui::InputInt("Disable pass", &diagnostics.config.disable_pass_index);
    ImGui::InputInt("Forced output texture", &diagnostics.config.force_output_texture_slot);
    ImGui::TextDisabled("Shader texture overrides are available in the selected layer's Effects inspector.");
}

void Debugger::drawLogsTab() {
    static bool show_warnings = true;
    static bool show_errors = true;
    ImGui::Checkbox("Warnings", &show_warnings);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &show_errors);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) logger_clear_recent_entries();
    ImGui::Separator();

    ImGui::BeginChild("RuntimeLogs", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const RuntimeLogEntry& entry : logger_recent_entries()) {
        if (entry.level == LOG_LEVEL_WARN && !show_warnings) continue;
        if (entry.level == LOG_LEVEL_ERROR && !show_errors) continue;
        if (entry.level != LOG_LEVEL_WARN && entry.level != LOG_LEVEL_ERROR) continue;

        const ImVec4 color =
            entry.level == LOG_LEVEL_ERROR ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(1.0f, 0.82f, 0.25f, 1.0f);
        ImGui::TextColored(color, "[%s]", entry.tag.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.message.c_str());
    }
    ImGui::EndChild();
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
    ImGui::BeginChild("SandboxPreview", ImVec2(0.0f, 0.0f), true);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "RENDERED PREVIEW");
    ImGui::Separator();
    ImGui::TextWrapped("The selected Wallpaper Engine preview project is rendered in this fixed preview area.");
    if (!g_sandbox_status.empty()) ImGui::TextWrapped("%s", g_sandbox_status.c_str());
    ImGui::TextDisabled("Engine: %s", ctx.engine_path);
    ImGui::EndChild();
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
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("DebugWorkspace", nullptr, flags);
        if (ImGui::BeginTabBar("DebugTabs")) {
            if (ImGui::BeginTabItem("Scene")) {
                drawSceneTab(ctx);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Diagnostics")) {
                drawDiagnosticsTab(ctx);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Logs")) {
                drawLogsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    simgui_render();
}

#endif  // DEBUG_BUILD
