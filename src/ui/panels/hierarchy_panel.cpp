#include "ui/panels/hierarchy_panel.h"

#if DEBUG_BUILD

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "shared/core/engine_context.h"
#include "sokol_app.h"
#include "ui/widgets/visibility_solo_controls.h"
#include "util/sokol_imgui.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {

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

void drawPanel(EngineContext& ctx) {
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
}  // namespace

void HierarchyPanel::draw(EngineContext& ctx) {
    drawPanel(ctx);
}

#endif  // DEBUG_BUILD
