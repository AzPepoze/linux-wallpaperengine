#include "ui/debugger.h"

#if DEBUG_BUILD

#include <algorithm>
#include <cstring>

#include "core/engine_context.h"
#include "imgui.h"
#include "render/diagnostics/render_diagnostics.h"

void Debugger::drawDiagnosticsTab(EngineContext& ctx) {
    RenderDiagnostics& diagnostics = RenderDiagnostics::instance();
    ImGui::Text("Performance");
    ImGui::Separator();
    ImGui::Text("CPU frame: %.3f ms", ctx.profiler.frame_ms);
    ImGui::Text("Rolling average: %.3f ms", ctx.profiler.frame_avg_ms);
    ImGui::Text("Peak: %.3f ms", ctx.profiler.frame_peak_ms);
    ImGui::Text("Update: %.3f ms  Render: %.3f ms  UI: %.3f ms", ctx.profiler.update_ms, ctx.profiler.render_ms,
                ctx.profiler.ui_ms);
    ImGui::Text("Draw calls: %u  Frame: %llu", ctx.profiler.draw_calls,
                static_cast<unsigned long long>(ctx.profiler.frame_index));
    if (ImGui::Button("Reset peak")) ctx.profiler.frame_peak_ms = ctx.profiler.frame_ms;
    ImGui::Spacing();
    ImGui::Text("Render capture");
    ImGui::Separator();
    ImGui::Checkbox("Enable diagnostic capture", &diagnostics.config.enabled);
    int capture_frame = static_cast<int>(diagnostics.config.target_frame);
    if (ImGui::InputInt("Capture frame", &capture_frame, 1, 60)) {
        diagnostics.config.target_frame = static_cast<uint64_t>(std::max(0, capture_frame));
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

#endif
