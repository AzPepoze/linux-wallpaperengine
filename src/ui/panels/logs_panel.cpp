#include "ui/debugger.h"

#if DEBUG_BUILD

#include "core/logger.h"
#include "imgui.h"

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

#endif
