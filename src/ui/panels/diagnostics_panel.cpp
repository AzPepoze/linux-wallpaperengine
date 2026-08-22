#include "ui/debugger.h"

#if DEBUG_BUILD

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
    ImGui::Text("Render Diagnostics (Auto-capture)");
    ImGui::Separator();
    ImGui::Text("Target frame: %llu", static_cast<unsigned long long>(diagnostics.config.target_frame));
    ImGui::Text("Output directory: %s", diagnostics.config.output_dir.c_str());
    const char* status = diagnostics.config.capture_complete
                             ? "Complete (Written to disk)"
                             : (diagnostics.is_capturing_frame ? "Capturing..." : "Pending (Runs at frame 100)");
    ImGui::Text("Capture status: %s", status);
}

#endif
