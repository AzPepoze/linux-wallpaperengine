#include "ui/debugger.h"

#if DEBUG_BUILD

#include "imgui.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "ui/widgets/ui_components.h"

void Debugger::drawDiagnosticsTab(EngineContext& ctx) {
    RenderDiagnostics& diagnostics = RenderDiagnostics::instance();

    // 1. Performance Overview & Real-Time Plots
    UiComponents::SectionHeader("Performance", "Frame Metrics");

    const float current_fps = ctx.profiler.frame_ms > 0.001 ? static_cast<float>(1000.0 / ctx.profiler.frame_ms) : 0.0f;
    const float avg_fps =
        ctx.profiler.frame_avg_ms > 0.001 ? static_cast<float>(1000.0 / ctx.profiler.frame_avg_ms) : 0.0f;

    char overlay_buf[64] = {};
    snprintf(overlay_buf, sizeof(overlay_buf), "%.1f FPS (%.2f ms)", current_fps, ctx.profiler.frame_ms);

    float max_scale = static_cast<float>(ctx.profiler.frame_peak_ms);
    if (max_scale < 18.0f) max_scale = 18.0f;
    if (max_scale > 50.0f) max_scale = 50.0f;

    UiComponents::TimelinePlot("##FrameHistory", ctx.profiler.frame_history,
                               static_cast<int>(profiler_stats_t::HISTORY_SIZE),
                               static_cast<int>(ctx.profiler.history_offset), overlay_buf, 0.0f, max_scale, 70.0f);

    UiComponents::PropertyRow("Framerate:", "%.1f FPS (Avg: %.1f FPS)", current_fps, avg_fps);
    UiComponents::PropertyRow("Frame Time:", "%.3f ms (Avg: %.3f ms | Peak: %.3f ms)", ctx.profiler.frame_ms,
                              ctx.profiler.frame_avg_ms, ctx.profiler.frame_peak_ms);
    UiComponents::PropertyRow("Stage Time:", "Update: %.3f ms | Render: %.3f ms | UI: %.3f ms", ctx.profiler.update_ms,
                              ctx.profiler.render_ms, ctx.profiler.ui_ms);
    UiComponents::PropertyRow("Draw Calls:", "%u (Frame: %llu)", ctx.profiler.draw_calls,
                              static_cast<unsigned long long>(ctx.profiler.frame_index));

    if (ImGui::Button("Reset Peak")) {
        ctx.profiler.frame_peak_ms = ctx.profiler.frame_ms;
    }

    // 2. Stage Breakdown Mini Plots
    if (ImGui::CollapsingHeader("Stage Timing Breakdown", ImGuiTreeNodeFlags_None)) {
        char update_overlay[32] = {};
        snprintf(update_overlay, sizeof(update_overlay), "Update: %.2f ms", ctx.profiler.update_ms);
        UiComponents::TimelinePlot("##UpdateHistory", ctx.profiler.update_history,
                                   static_cast<int>(profiler_stats_t::HISTORY_SIZE),
                                   static_cast<int>(ctx.profiler.history_offset), update_overlay, 0.0f, 16.6f, 40.0f);

        char render_overlay[32] = {};
        snprintf(render_overlay, sizeof(render_overlay), "Render: %.2f ms", ctx.profiler.render_ms);
        UiComponents::TimelinePlot("##RenderHistory", ctx.profiler.render_history,
                                   static_cast<int>(profiler_stats_t::HISTORY_SIZE),
                                   static_cast<int>(ctx.profiler.history_offset), render_overlay, 0.0f, 16.6f, 40.0f);
    }

    // 3. GPU & Hardware Acceleration
    UiComponents::SectionHeader("GPU & Hardware Acceleration");
    const auto& gpu = GpuDeviceManager::instance().getSelectedGpu();
    UiComponents::PropertyRow("Active GPU:", "[%u] %s", gpu.index, gpu.name.empty() ? "Default" : gpu.name.c_str());
    UiComponents::PropertyRow("Type / PCI:", "%s | PCI: %s", gpu.device_type.c_str(),
                              gpu.pci_bus_id.empty() ? "N/A" : gpu.pci_bus_id.c_str());
    UiComponents::PropertyRow("DRM Node:", "%s", gpu.drm_render_node.empty() ? "N/A" : gpu.drm_render_node.c_str());
    UiComponents::StatusBadge("VA-API Driver:", gpu.vaapi_supported, gpu.vaapi_driver.c_str(), "Not Available");

    // 4. Video Pipeline Performance & Zero-Copy VRAM Cache
    const auto& videos = ctx.asset_mgr.getVideoTextures();
    if (!videos.empty()) {
        UiComponents::SectionHeader("Video Pipeline", "Zero-Copy HW");
        for (size_t i = 0; i < videos.size(); ++i) {
            const auto& v = videos[i];
            if (v.decoder) {
                const auto& m = v.decoder->getMetrics();
                const auto& s = v.decoder->getStats();
                const auto& t = v.decoder->getTiming();
                const float stream_fps =
                    v.decoder->frameDuration() > 0.0001f ? (1.0f / v.decoder->frameDuration()) : 0.0f;
                UiComponents::PropertyRow("Stream:", "%s (%ux%u @ %.1f FPS)",
                                          v.decoder->isZeroCopy() ? "Zero-Copy VA-API" : "Software Fallback",
                                          v.decoder->width(), v.decoder->height(), stream_fps);

                const uint64_t total_cache = m.import_cache_hits + m.import_cache_misses;
                const float hit_rate = total_cache > 0
                                           ? static_cast<float>((100.0 * static_cast<double>(m.import_cache_hits)) /
                                                                static_cast<double>(total_cache))
                                           : 100.0f;

                UiComponents::PercentageBar("VRAM Cache Hit Rate", hit_rate / 100.0f, 18.0f);

                UiComponents::PropertyRow("HW Decoded:", "%llu frames (Packets: %llu, Misses: %llu)",
                                          static_cast<unsigned long long>(m.vaapi_frames_decoded),
                                          static_cast<unsigned long long>(s.packets_read),
                                          static_cast<unsigned long long>(m.import_cache_misses));
                UiComponents::PropertyRow("CPU Copies:", "%llu B (sws_scale: %llu calls)",
                                          static_cast<unsigned long long>(m.cpu_rgba_bytes),
                                          static_cast<unsigned long long>(m.sws_scale_calls));
                UiComponents::PropertyRow(
                    "Latencies:", "Demux: %.2f ms | Decode: %.2f ms | VA Sync: %.2f ms | Sched: %.2f ms",
                    t.demux_cpu_ms, t.decode_submit_cpu_ms, t.va_sync_cpu_ms, t.scheduler_cpu_ms);
            }
        }
    }

    // 5. Diagnostics Capture
    UiComponents::SectionHeader("Diagnostics Capture");
    const char* status =
        diagnostics.config.capture_complete
            ? "Complete (Written to disk)"
            : (diagnostics.is_capturing_frame ? "Capturing..." : (diagnostics.config.enabled ? "Pending..." : "Idle"));
    UiComponents::PropertyRow("Status:", "%s", status);
    if (!diagnostics.config.output_dir.empty()) {
        UiComponents::PropertyRow("Output:", "%s", diagnostics.config.output_dir.c_str());
    }
    if (ImGui::Button("Dump Diagnostics Now", ImVec2(-1.0f, 28.0f))) {
        diagnostics.triggerCapture(ctx.profiler.frame_index);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Dumps shader passes, textures, render graph, and uniforms to ./diagnostics");
    }
}

#endif
