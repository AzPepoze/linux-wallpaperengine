#include "ui/debugger.h"

#if DEBUG_BUILD

#include "core/engine_context.h"
#include "core/gpu_device_manager.h"
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
    ImGui::Text("GPU & Hardware Acceleration");
    ImGui::Separator();
    const auto& gpu = GpuDeviceManager::instance().getSelectedGpu();
    ImGui::Text("Active GPU: [%u] %s", gpu.index, gpu.name.empty() ? "Default" : gpu.name.c_str());
    ImGui::Text("Type: %s | PCI: %s", gpu.device_type.c_str(), gpu.pci_bus_id.empty() ? "N/A" : gpu.pci_bus_id.c_str());
    ImGui::Text("DRM Node: %s", gpu.drm_render_node.empty() ? "N/A" : gpu.drm_render_node.c_str());
    ImGui::Text("VA-API Driver: %s", gpu.vaapi_supported ? gpu.vaapi_driver.c_str() : "Not Available");

    const auto& videos = ctx.asset_mgr.getVideoTextures();
    if (!videos.empty()) {
        ImGui::Spacing();
        ImGui::Text("Video Pipeline (Zero-Copy HW)");
        ImGui::Separator();
        for (size_t i = 0; i < videos.size(); ++i) {
            const auto& v = videos[i];
            if (v.decoder) {
                const auto& m = v.decoder->getMetrics();
                const auto& s = v.decoder->getStats();
                const auto& t = v.decoder->getTiming();
                ImGui::Text("Video [%zu]: %s (%ux%u, %.1f FPS)", i,
                            v.decoder->isZeroCopy() ? "Zero-Copy VAAPI" : "Software Fallback", v.decoder->width(),
                            v.decoder->height(), 1.0f / v.decoder->frameDuration());
                ImGui::Text("  HW Decoded: %llu | Cache Hits: %llu | Misses: %llu",
                            (unsigned long long)m.vaapi_frames_decoded, (unsigned long long)m.import_cache_hits,
                            (unsigned long long)m.import_cache_misses);
                ImGui::Text("  sws_scale: %llu | CPU RGBA: %llu B | Packets: %llu",
                            (unsigned long long)m.sws_scale_calls, (unsigned long long)m.cpu_rgba_bytes,
                            (unsigned long long)s.packets_read);
                ImGui::Text("  Demux: %.3f ms | Decode: %.3f ms | VA Sync: %.3f ms | Sched: %.3f ms", t.demux_cpu_ms,
                            t.decode_submit_cpu_ms, t.va_sync_cpu_ms, t.scheduler_cpu_ms);
            }
        }
    }
}

#endif
