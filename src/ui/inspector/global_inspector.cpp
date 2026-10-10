#include "global_inspector.h"

#if DEBUG_BUILD

#include <algorithm>
#include <cstdio>

#include "imgui.h"
#include "shared/core/engine_context.h"
#include "shared/core/resolution.h"
#include "shared/graphics/backend/performance_profile.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "ui/widgets/ui_components.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/web/web_transport.h"

namespace Inspector {
namespace {
constexpr size_t kTopGpuPasses = 8;

void showGpuPasses(const EngineContext& ctx) {
    if (!ctx.performance_profile) {
        ImGui::TextDisabled("Start with --performance-profile for GPU timings");
        return;
    }
    const auto& spans = performance_profile::gpuSpanStats();
    if (spans.empty()) {
        ImGui::TextDisabled("Waiting for the first 10 s GPU sample");
        return;
    }
    ImGui::TextUnformatted("GPU, slowest passes (last 10 s):");
    const size_t shown = std::min(spans.size(), kTopGpuPasses);
    for (size_t i = 0; i < shown; ++i) ImGui::Text("%s: %.3f ms", spans[i].label.c_str(), spans[i].mean_ms);
}

// Output size, frame timing, render sizes of image layers, and the GPU cost of the last window.
void showRender(EngineContext& ctx) {
    if (!ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Text("Resolution: %s", resolution::describe(ctx.resolution).c_str());
    ImGui::Text("Output: %.0fx%.0f (physical %.0fx%.0f)", ctx.renderer.view_width, ctx.renderer.view_height,
                ctx.scene.physical_view_width, ctx.scene.physical_view_height);
    ImGui::Text("Frame: %.2f ms avg | %.2f ms peak | %.1f FPS", ctx.profiler.frame_avg_ms, ctx.profiler.frame_peak_ms,
                ctx.profiler.measured_fps);
    char overlay[32] = {};
    snprintf(overlay, sizeof(overlay), "%.2f ms", ctx.profiler.frame_ms);
    UiComponents::TimelinePlot("##FrameHistory", ctx.profiler.frame_history,
                               static_cast<int>(profiler_stats_t::HISTORY_SIZE),
                               static_cast<int>(ctx.profiler.history_offset), overlay, 0.0f, 33.3f, 50.0f);

    size_t targeted_layers = 0;
    long long authored_pixels = 0;
    long long target_pixels = 0;
    for (const Layer* layer : ctx.scene.layers) {
        const auto* image = dynamic_cast<const ImageLayer*>(layer);
        if (!image) continue;
        const ImageLayer::RenderSizeInfo size = image->renderSizeInfo();
        if (size.target_width <= 0) continue;
        ++targeted_layers;
        authored_pixels += static_cast<long long>(size.authored_width) * size.authored_height;
        target_pixels += static_cast<long long>(size.target_width) * size.target_height;
    }
    ImGui::Text("Layers with a render target: %zu", targeted_layers);
    if (authored_pixels > 0) {
        ImGui::Text("Target pixels: %.2f M of %.2f M authored (%.0f%%)", target_pixels / 1.0e6, authored_pixels / 1.0e6,
                    100.0 * target_pixels / authored_pixels);
    }
    showGpuPasses(ctx);
}
}  // namespace

void GlobalInspector::show(EngineContext& ctx) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "GLOBAL ENGINE SETTINGS");
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Mouse source: %s", ctx.input.pointer_source.c_str());
    if (ctx.web_frame_transport != "none") {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Web frames: %s (requested: %s)",
                           ctx.web_frame_transport.c_str(), webTransportName(ctx.web.transport));
    }
    const char* modes[] = {"Cover", "Fit", "Stretch"};
    int current_mode = (int)ctx.scene.scaling_mode;
    if (ImGui::Combo("Scaling Mode", &current_mode, modes, IM_ARRAYSIZE(modes))) {
        ctx.scene.scaling_mode = (scaling_mode_t)current_mode;
    }
    ImGui::Text("Resolution: %.0f x %.0f", ctx.scene.scene_w, ctx.scene.scene_h);
    ImGui::Text("Render Scale: %.3f (Offsets: %.1f, %.1f)", ctx.scene.render_scale, ctx.scene.offset_x,
                ctx.scene.offset_y);
    ImGui::Text("FPS: %.1f | CPU frame: %.2f ms", ctx.profiler.measured_fps, ctx.profiler.frame_avg_ms);
    showRender(ctx);
    ImGui::Checkbox("Show node IDs in scene tree", &ctx.debug.show_node_ids);

    ImGui::Separator();
    ImGui::TextDisabled("Wallpaper Path");
    static double copied_until = 0.0;
    ImGui::SameLine();
    ImGui::BeginDisabled(ctx.wallpaper_path[0] == '\0');
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(ctx.wallpaper_path);
        copied_until = ImGui::GetTime() + 1.5;
    }
    ImGui::EndDisabled();
    if (ImGui::GetTime() < copied_until) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Copied!");
    }
    ImGui::TextWrapped("%s", ctx.wallpaper_path[0] != '\0' ? ctx.wallpaper_path : "(none)");

    if (ImGui::CollapsingHeader("Camera & Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputFloat3("Eye Position", ctx.scene.camera.eye.data());
        ImGui::InputFloat3("Center LookAt", ctx.scene.camera.center.data());
        ImGui::InputFloat3("Up Vector", ctx.scene.camera.up.data());
        ImGui::DragFloat("FOV", &ctx.scene.general.fov, 0.5f, 1.0f, 179.0f);
        ImGui::DragFloat("Near Z", &ctx.scene.general.near_z, 0.001f, 0.001f, 10.0f, "%.5f");
        ImGui::DragFloat("Far Z", &ctx.scene.general.far_z, 10.0f, 10.0f, 100000.0f);
        ImGui::DragFloat2("Orthographic Extent", ctx.scene.general.orthogonal_projection.data(), 1.0f, 0.0f, 100000.0f);
        ImGui::DragFloat("Zoom", &ctx.scene.general.zoom, 0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("Perspective Particle FOV", &ctx.scene.general.perspective_override_fov, 0.5f, 0.0f, 179.0f);
        ImGui::Checkbox("Camera Fade", &ctx.scene.general.camera_fade);
        ImGui::SameLine();
        ImGui::Checkbox("Camera Preview", &ctx.scene.general.camera_preview);
    }

    if (ImGui::CollapsingHeader("Parallax & Camera Shake", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Parallax Enabled", &ctx.parallax.enabled);
        ImGui::DragFloat("Parallax Amount", &ctx.parallax.amount, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Parallax Delay", &ctx.parallax.delay, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Mouse Influence", &ctx.parallax.mouse_influence, 0.005f, 0.0f, 1.0f);
        ImGui::TextDisabled("Pointer: (%.3f, %.3f) | Smooth: (%.3f, %.3f)", ctx.parallax.pointer_x,
                            ctx.parallax.pointer_y, ctx.parallax.smooth_x, ctx.parallax.smooth_y);

        ImGui::Separator();
        ImGui::Checkbox("Camera Shake Enabled", &ctx.shake.enabled);
        ImGui::DragFloat("Shake Amplitude", &ctx.shake.amplitude, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Shake Speed", &ctx.shake.speed, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Shake Roughness", &ctx.shake.roughness, 0.01f, 0.0f, 2.0f);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Live Shake Offset: (%.2f px, %.2f px)", ctx.shake.x,
                           ctx.shake.y);
    }

    if (ImGui::CollapsingHeader("Lighting & Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient Color", ctx.scene.general.ambient_color.data());
        ImGui::ColorEdit3("Skylight Color", ctx.scene.general.skylight_color.data());
        ImGui::ColorEdit4("Clear Color", ctx.scene.general.clear_color.data());
        ImGui::Checkbox("Clear Enabled", &ctx.scene.general.clear_enabled);
        ImGui::SameLine();
        ImGui::Checkbox("HDR Accumulation", &ctx.scene.general.hdr);
    }

    if (ImGui::CollapsingHeader("Bloom & HDR Post-Process", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Bloom Enabled", &ctx.scene.general.bloom.enabled);
        ImGui::DragFloat("LDR Strength", &ctx.scene.general.bloom.strength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("LDR Threshold", &ctx.scene.general.bloom.threshold, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("HDR Scatter", &ctx.scene.general.bloom.hdr_scatter, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("HDR Strength", &ctx.scene.general.bloom.hdr_strength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("HDR Threshold", &ctx.scene.general.bloom.hdr_threshold, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("HDR Feather", &ctx.scene.general.bloom.hdr_feather, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("HDR Iterations", &ctx.scene.general.bloom.hdr_iterations, 1.0f, 1.0f, 16.0f);
    }

    if (ImGui::CollapsingHeader("Diagnostics & Dump", ImGuiTreeNodeFlags_DefaultOpen)) {
        RenderDiagnostics& diagnostics = RenderDiagnostics::instance();
        const char* status =
            diagnostics.config.capture_complete
                ? "Complete (Written to disk)"
                : (diagnostics.is_capturing_frame ? "Capturing..."
                                                  : (diagnostics.config.enabled ? "Pending..." : "Idle"));
        ImGui::Text("Status: %s", status);
        if (!diagnostics.config.output_dir.empty()) {
            ImGui::TextDisabled("Output: %s", diagnostics.config.output_dir.c_str());
        }
        if (ImGui::Button("Dump Diagnostics Now", ImVec2(-1.0f, 28.0f))) {
            diagnostics.triggerCapture(ctx.profiler.frame_index);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Dumps shader passes, textures, render graph, and uniforms to ./diagnostics");
        }
    }
}

}  // namespace Inspector

#endif  // DEBUG_BUILD
