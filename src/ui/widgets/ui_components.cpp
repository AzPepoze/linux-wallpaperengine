#include "ui/widgets/ui_components.h"

#if DEBUG_BUILD

#include <cstdio>

namespace UiComponents {

void SectionHeader(const char* title, const char* subtitle) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", title);
    if (subtitle && subtitle[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", subtitle);
    }
    ImGui::Separator();
}

void PropertyRow(const char* label, const char* value_fmt, ...) {
    char buffer[512] = {};
    va_list args;
    va_start(args, value_fmt);
    vsnprintf(buffer, sizeof(buffer), value_fmt, args);
    va_end(args);

    ImGui::TextDisabled("%-16s", label);
    ImGui::SameLine();
    ImGui::TextUnformatted(buffer);
}

void StatusBadge(const char* label, bool active, const char* active_text, const char* inactive_text) {
    ImGui::TextDisabled("%-16s", label);
    ImGui::SameLine();
    if (active) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "%s", active_text ? active_text : "Active");
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", inactive_text ? inactive_text : "Inactive");
    }
}

void MetricCard(const char* label, const char* value, const char* subtext, ImVec4 accent_color) {
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    ImGui::TextColored(accent_color, "%s", value);
    if (subtext && subtext[0] != '\0') {
        ImGui::TextDisabled("%s", subtext);
    }
    ImGui::EndGroup();
}

void TimelinePlot(const char* id, const float* values, int count, int offset, const char* overlay, float scale_min,
                  float scale_max, float height) {
    if (!values || count <= 0) return;
    ImGui::PlotLines(id, values, count, offset, overlay, scale_min, scale_max, ImVec2(-1.0f, height));
}

void PercentageBar(const char* label, float percentage, float height) {
    const float clamped = percentage < 0.0f ? 0.0f : (percentage > 1.0f ? 1.0f : percentage);
    char overlay[64] = {};
    snprintf(overlay, sizeof(overlay), "%s: %.1f%%", label ? label : "Progress", clamped * 100.0f);
    ImGui::ProgressBar(clamped, ImVec2(-1.0f, height), overlay);
}

}  // namespace UiComponents

#endif  // DEBUG_BUILD
