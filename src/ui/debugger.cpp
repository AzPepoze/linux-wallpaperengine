#include "debugger.h"

#include "shared/graphics/backend/surface.h"

#if DEBUG_BUILD

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "sandbox_catalog.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "sokol_app.h"
#include "sokol_log.h"
#include "ui/inspector/global_inspector.h"
#include "ui/inspector/layer_inspector.h"
#include "ui/widgets/visibility_solo_controls.h"
#include "util/sokol_imgui.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
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

std::unordered_set<uint32_t> g_expanded_nodes;
const SceneTree* g_expansion_tree = nullptr;
bool g_scroll_to_selection = false;

int findLayerIndex(const EngineContext& ctx, uint32_t scene_object_id) {
    for (int index = 0; index < (int)ctx.scene.layers.size(); ++index) {
        if (ctx.scene.layers[index]->scene_object_id == scene_object_id) return index;
    }
    return -1;
}

void syncExpansionState(EngineContext& ctx) {
    if (ctx.scene.scene_tree != g_expansion_tree) {
        g_expanded_nodes.clear();
        g_expansion_tree = ctx.scene.scene_tree;
    }
}

struct TreeRow {
    uint32_t id = 0;
    uint32_t parent_id = 0;
    bool has_children = false;
    bool expanded = false;
};

void collectVisibleRows(const SceneTree& tree, uint32_t node_id, std::vector<TreeRow>& out) {
    const SceneTreeNode* node = tree.find(node_id);
    if (!node) return;
    const bool has_children = !node->children.empty();
    const bool expanded = has_children && g_expanded_nodes.count(node->id) > 0;
    out.push_back({node->id, node->parent_id, has_children, expanded});
    if (expanded) {
        for (uint32_t child_id : node->children) collectVisibleRows(tree, child_id, out);
    }
}

void handleTreeKeyboard(EngineContext& ctx, const std::vector<TreeRow>& rows) {
    if ((!ImGui::IsWindowFocused() && !ImGui::IsWindowHovered()) || ImGui::GetIO().WantTextInput) return;

    int cursor = -1;
    if (ctx.debug.selected_node_id != 0) {
        for (int i = 0; i < (int)rows.size(); ++i) {
            if (rows[i].id == ctx.debug.selected_node_id) {
                cursor = i;
                break;
            }
        }
    }

    int next = cursor;
    bool nav = false;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        if (cursor > -1) {
            next = cursor - 1;
            nav = true;
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        if (cursor < (int)rows.size() - 1) {
            next = cursor + 1;
            nav = true;
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        if (cursor >= 0 && rows[cursor].has_children) {
            if (!rows[cursor].expanded) {
                g_expanded_nodes.insert(rows[cursor].id);
                nav = true;
            } else if (cursor + 1 < (int)rows.size()) {
                next = cursor + 1;
                nav = true;
            }
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        if (cursor >= 0) {
            if (rows[cursor].has_children && rows[cursor].expanded) {
                g_expanded_nodes.erase(rows[cursor].id);
                nav = true;
            } else {
                for (int i = 0; i < (int)rows.size(); ++i) {
                    if (rows[i].id == rows[cursor].parent_id) {
                        next = i;
                        nav = true;
                        break;
                    }
                }
            }
        }
    }

    if (!nav) return;

    if (next == -1) {
        ctx.debug.selected_object = -1;
        ctx.debug.selected_node_id = 0;
    } else {
        ctx.debug.selected_node_id = rows[next].id;
        ctx.debug.selected_object = findLayerIndex(ctx, rows[next].id);
    }
    g_scroll_to_selection = true;
}

void drawSceneNode(EngineContext& ctx, const SceneTreeNode& node) {
    const int layer_index = findLayerIndex(ctx, node.id);
    const bool has_children = !node.children.empty();
    const bool is_leaf = !has_children;
    const bool is_selected = ctx.debug.selected_node_id != 0
                                 ? ctx.debug.selected_node_id == node.id
                                 : (layer_index >= 0 && ctx.debug.selected_object == layer_index);
    const bool expanded = has_children && g_expanded_nodes.count(node.id) > 0;
    std::string node_name = node.name.empty() ? "Node " + std::to_string(node.id) : node.name;
    if (layer_index >= 0 && layer_index < (int)ctx.scene.layers.size()) {
        const Layer* layer = ctx.scene.layers[layer_index];
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
        UiWidgets::drawVisibilitySoloControls(*ctx.scene.layers[layer_index], "Toggle layer visibility", "Solo layer");
        ImGui::SameLine();
    }
    if (has_children) ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
    const bool open = ImGui::TreeNodeEx(node_name.c_str(), flags);
    if (has_children && ImGui::IsItemToggledOpen()) {
        if (expanded)
            g_expanded_nodes.erase(node.id);
        else
            g_expanded_nodes.insert(node.id);
    }
    if (ImGui::IsItemClicked()) {
        ctx.debug.selected_node_id = node.id;
        ctx.debug.selected_object = layer_index;
        if (layer_index >= 0 && ImGui::GetIO().KeyCtrl)
            ctx.scene.layers[layer_index]->setSolo(!ctx.scene.layers[layer_index]->solo);
    }
    if (is_selected && g_scroll_to_selection) ImGui::SetScrollHereY(0.5f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scene node %u", node.id);

    if (open && has_children) {
        for (uint32_t child_id : node.children) {
            const SceneTreeNode* child = ctx.scene.scene_tree->find(child_id);
            if (child) drawSceneNode(ctx, *child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void drawHierarchyPanel(EngineContext& ctx) {
    g_scroll_to_selection = false;
    const double fps = ctx.profiler.frame_avg_ms > 0.0 ? (1000.0 / ctx.profiler.frame_avg_ms) : 0.0;
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "SCENE TREE");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.1f FPS)", fps);
    ImGui::Separator();

    syncExpansionState(ctx);

    const bool has_tree = ctx.scene.scene_tree && ctx.scene.scene_tree->size() > 0;
    if (has_tree) {
        std::vector<TreeRow> rows;
        for (uint32_t root_id : ctx.scene.scene_tree->rootIds()) {
            collectVisibleRows(*ctx.scene.scene_tree, root_id, rows);
        }
        handleTreeKeyboard(ctx, rows);
    }

    const float isolate_btn_w = ImGui::CalcTextSize("Isolate: ON").x + ImGui::GetStyle().FramePadding.x * 2.0f + 10.0f;
    const float available_w = ImGui::GetContentRegionAvail().x;
    const float selectable_w = available_w - isolate_btn_w - ImGui::GetStyle().ItemSpacing.x;

    const bool global_selected = ctx.debug.selected_object == -1 && ctx.debug.selected_node_id == 0;
    if (ImGui::Selectable("Global Settings", global_selected, 0,
                          ImVec2(selectable_w > 40.0f ? selectable_w : 0.0f, 0))) {
        ctx.debug.selected_object = -1;
        ctx.debug.selected_node_id = 0;
    }
    ImGui::SameLine();
    if (ctx.debug.test_mode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
    }
    if (ImGui::Button(ctx.debug.test_mode ? "Isolate: ON" : "Isolate", ImVec2(isolate_btn_w, 0))) {
        ctx.debug.test_mode = !ctx.debug.test_mode;
    }
    if (ctx.debug.test_mode) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Render only the selected layer");

    ImGui::Separator();
    if (has_tree) {
        for (uint32_t root_id : ctx.scene.scene_tree->rootIds()) {
            const SceneTreeNode* root = ctx.scene.scene_tree->find(root_id);
            if (root) drawSceneNode(ctx, *root);
        }
        return;
    }

    for (int index = 0; index < (int)ctx.scene.layers.size(); ++index) {
        Layer* layer = ctx.scene.layers[index];
        ImGui::PushID(index);
        UiWidgets::drawVisibilitySoloControls(*layer, "Toggle layer visibility", "Solo layer");
        ImGui::SameLine();
        if (ImGui::Selectable(layer->name.c_str(), ctx.debug.selected_object == index))
            ctx.debug.selected_object = index;
        ImGui::PopID();
    }
}

void drawInspectorPanel(EngineContext& ctx) {
    const double fps = ctx.profiler.frame_avg_ms > 0.0 ? (1000.0 / ctx.profiler.frame_avg_ms) : 0.0;
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "INSPECTOR");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[ %.1f FPS | %.1f ms ]", fps, ctx.profiler.frame_avg_ms);
    ImGui::Separator();

    if (ctx.debug.selected_object == -1 && ctx.debug.selected_node_id == 0) {
        Inspector::GlobalInspector::show(ctx);
        return;
    }

    if (ctx.debug.selected_object >= 0 && ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        Layer* layer = ctx.scene.layers[ctx.debug.selected_object];
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
    desc.disable_set_mouse_cursor = surface::hasProvider();
    simgui_setup(&desc);
    if (surface::hasProvider()) {
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = nullptr;
        ImGui::GetPlatformIO().Platform_GetClipboardTextFn = nullptr;
    }
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
    ImGui::BeginChild("SceneTree", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoNavInputs);
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

    if (ctx.debug.selected_object >= 0 && ctx.debug.selected_object < (int)ctx.scene.layers.size()) {
        ctx.scene.layers[ctx.debug.selected_object]->drawDebug(ctx);
    }
}

void Debugger::drawSandbox(EngineContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)surface::width(), (float)surface::height()), ImGuiCond_Always);
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
    const float dpi_scale = surface::dpiScale();
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
    frame_desc.width = surface::width();
    frame_desc.height = surface::height();
    frame_desc.delta_time = (float)surface::frameDuration();
    frame_desc.dpi_scale = surface::dpiScale();
    simgui_new_frame(&frame_desc);

    if (ctx.debug.show_ui) {
        if (ctx.runtime_mode == RuntimeMode::Sandbox) {
            drawSandbox(ctx);
        } else {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)surface::width(), (float)surface::height()), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::Begin("DebugWorkspace", nullptr, flags);
            drawSceneTab(ctx);
            ImGui::End();

            if (g_logs_open) {
                const float max_width = std::max(360.0f, (float)surface::width() - 32.0f);
                const float max_height = std::max(240.0f, (float)surface::height() - 32.0f);
                const ImVec2 log_window_size(std::min(720.0f, max_width), std::min(420.0f, max_height));
                ImGui::SetNextWindowPos(ImVec2(((float)surface::width() - log_window_size.x) * 0.5f,
                                               ((float)surface::height() - log_window_size.y) * 0.5f),
                                        ImGuiCond_Appearing);
                ImGui::SetNextWindowSize(log_window_size, ImGuiCond_Appearing);
                ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 240.0f), ImVec2(max_width, max_height));
                if (ImGui::Begin("Logs", &g_logs_open)) {
                    drawLogsTab();
                }
                ImGui::End();
            }
        }
    }
    simgui_render();
}

#endif  // DEBUG_BUILD
