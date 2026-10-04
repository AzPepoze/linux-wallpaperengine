#include "debugger.h"

#include "shared/graphics/backend/surface.h"

#if DEBUG_BUILD

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
#include "ui/panels/hierarchy_panel.h"
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

namespace {
// Standard font locations across distributions; the scan is skipped for directories that do not exist.
std::vector<std::string> fontDirectories() {
    std::vector<std::string> dirs = {"/usr/share/fonts", "/usr/local/share/fonts", "/run/host/fonts"};
    if (const char* home = getenv("HOME")) {
        dirs.push_back(std::string(home) + "/.local/share/fonts");
        dirs.push_back(std::string(home) + "/.fonts");
    }
    return dirs;
}

// First installed font whose file name contains any of `needles` (case-insensitive), or "".
std::string findFontFile(const std::vector<std::string>& needles) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const std::string& dir : fontDirectories()) {
        if (!fs::is_directory(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (extension != ".ttf" && extension != ".otf" && extension != ".ttc") continue;
            std::string name = it->path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            for (const std::string& needle : needles)
                if (name.find(needle) != std::string::npos) return it->path().string();
        }
    }
    return "";
}
#if defined(LWE_LAYER_SHELL)
// A layer surface has no sokol_app clipboard, so route ImGui's clipboard through wl-clipboard when it is installed.
const char* layerClipboardGet(ImGuiContext*) {
    static std::string text;
    text.clear();
    if (FILE* pipe = popen("wl-paste --no-newline 2>/dev/null", "r")) {
        char buffer[4096];
        size_t read = 0;
        while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) text.append(buffer, read);
        pclose(pipe);
    }
    return text.c_str();
}

void layerClipboardSet(ImGuiContext*, const char* text) {
    if (FILE* pipe = popen("wl-copy 2>/dev/null", "w")) {
        fwrite(text, 1, strlen(text), pipe);
        pclose(pipe);
    }
}
#endif
}  // namespace

void Debugger::init() {
    simgui_desc_t desc = {};
    desc.logger.func = slog_func;
    desc.disable_set_mouse_cursor = surface::hasProvider();
    desc.no_default_font = true;  // the default font plus script fallbacks are added below
    simgui_setup(&desc);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    ImFontConfig merge;
    merge.MergeMode = true;
    merge.PixelSnapH = true;
    const auto merge_font = [&](const std::string& path, const ImWchar* ranges) {
        if (!path.empty()) io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &merge, ranges);
    };
    // Merge whatever script fonts the system has, so non-Latin names render instead of "????".
    const std::string cjk = findFontFile(
        {"notosanscjk", "notoserifcjk", "sourcehansans", "sourcehanserif", "wqy", "wenquanyi",
         "droidsansfallback", "notosanssc", "notosansjp", "notosanskr", "unifont", "arpluming", "arplukai"});
    const std::string latin = findFontFile({"notosans-regular", "notosans", "dejavusans", "liberationsans",
                                            "freesans", "arial", "segoeui"});
    const std::string thai = findFontFile({"notosansthai", "garuda", "norasi", "dejavusans", "freesans"});
    if (!cjk.empty()) {
        merge_font(cjk, io.Fonts->GetGlyphRangesChineseFull());
        merge_font(cjk, io.Fonts->GetGlyphRangesJapanese());
        merge_font(cjk, io.Fonts->GetGlyphRangesKorean());
    }
    if (!latin.empty()) {
        merge_font(latin, io.Fonts->GetGlyphRangesCyrillic());
        merge_font(latin, io.Fonts->GetGlyphRangesGreek());
        merge_font(latin, io.Fonts->GetGlyphRangesVietnamese());
    }
    if (!thai.empty()) merge_font(thai, io.Fonts->GetGlyphRangesThai());
    // sokol_imgui rebuilds the font atlas texture automatically once fonts are added.

    if (surface::hasProvider()) {
#if defined(LWE_LAYER_SHELL)
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = layerClipboardSet;
        ImGui::GetPlatformIO().Platform_GetClipboardTextFn = layerClipboardGet;
#else
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = nullptr;
        ImGui::GetPlatformIO().Platform_GetClipboardTextFn = nullptr;
#endif
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
    HierarchyPanel::draw(ctx);
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
