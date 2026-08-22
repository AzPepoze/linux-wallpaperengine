#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

#include <cstdarg>
#include <string>
#include <vector>

#include "imgui.h"

namespace UiComponents {

// Section header with clean styling, optional subtitle and separator
void SectionHeader(const char* title, const char* subtitle = nullptr);

// Key-Value row with alignment and dim label
void PropertyRow(const char* label, const char* value_fmt, ...);

// Formatted status badge (e.g. Green for active, Gray/Red for inactive)
void StatusBadge(const char* label, bool active, const char* active_text, const char* inactive_text = "Disabled");

// Metric card with prominent value and optional subtitle / secondary text
void MetricCard(const char* label, const char* value, const char* subtext = nullptr,
                ImVec4 accent_color = ImVec4(0.2f, 0.8f, 1.0f, 1.0f));

// Smooth timeline plot with min/max scale and overlay text
void TimelinePlot(const char* id, const float* values, int count, int offset, const char* overlay, float scale_min,
                  float scale_max, float height = 65.0f);

// Colored progress / percentage bar with label
void PercentageBar(const char* label, float percentage, float height = 18.0f);

}  // namespace UiComponents

#endif  // DEBUG_BUILD

#endif  // UI_COMPONENTS_H
