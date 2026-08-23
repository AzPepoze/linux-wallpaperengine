#include "global_inspector.h"

#if DEBUG_BUILD

#include "imgui.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"

namespace Inspector {

void GlobalInspector::show(EngineContext& ctx) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "GLOBAL ENGINE SETTINGS");
    const char* modes[] = {"Cover", "Fit"};
    int current_mode = (int)ctx.scaling_mode;
    if (ImGui::Combo("Scaling Mode", &current_mode, modes, IM_ARRAYSIZE(modes))) {
        ctx.scaling_mode = (scaling_mode_t)current_mode;
    }
    ImGui::Text("Resolution: %.0f x %.0f", ctx.scene_w, ctx.scene_h);
    ImGui::Text("Render Scale: %.3f (Offsets: %.1f, %.1f)", ctx.render_scale, ctx.offset_x, ctx.offset_y);
    ImGui::Text("FPS: %.1f | Frame Time: %.2f ms", ImGui::GetIO().Framerate,
                1000.0f / (ImGui::GetIO().Framerate > 0.0f ? ImGui::GetIO().Framerate : 60.0f));

    if (ImGui::CollapsingHeader("Camera & Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputFloat3("Eye Position", ctx.camera.eye.data());
        ImGui::InputFloat3("Center LookAt", ctx.camera.center.data());
        ImGui::InputFloat3("Up Vector", ctx.camera.up.data());
        ImGui::DragFloat("FOV", &ctx.general.fov, 0.5f, 1.0f, 179.0f);
        ImGui::DragFloat("Near Z", &ctx.general.near_z, 0.001f, 0.001f, 10.0f, "%.5f");
        ImGui::DragFloat("Far Z", &ctx.general.far_z, 10.0f, 10.0f, 100000.0f);
        ImGui::DragFloat2("Orthographic Extent", ctx.general.orthogonal_projection.data(), 1.0f, 0.0f, 100000.0f);
        ImGui::DragFloat("Zoom", &ctx.general.zoom, 0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("Perspective Particle FOV", &ctx.general.perspective_override_fov, 0.5f, 0.0f, 179.0f);
        ImGui::Checkbox("Camera Fade", &ctx.general.camera_fade);
        ImGui::SameLine();
        ImGui::Checkbox("Camera Preview", &ctx.general.camera_preview);
    }

    if (ImGui::CollapsingHeader("Parallax & Camera Shake", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Parallax Enabled", &ctx.camera_parallax_enabled);
        ImGui::DragFloat("Parallax Amount", &ctx.camera_parallax_amount, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Parallax Delay", &ctx.camera_parallax_delay, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Mouse Influence", &ctx.camera_parallax_mouse_influence, 0.005f, 0.0f, 1.0f);
        ImGui::TextDisabled("Pointer: (%.3f, %.3f) | Smooth: (%.3f, %.3f)", ctx.parallax_pointer_x,
                            ctx.parallax_pointer_y, ctx.parallax_smooth_x, ctx.parallax_smooth_y);

        ImGui::Separator();
        ImGui::Checkbox("Camera Shake Enabled", &ctx.camera_shake_enabled);
        ImGui::DragFloat("Shake Amplitude", &ctx.camera_shake_amplitude, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Shake Speed", &ctx.camera_shake_speed, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Shake Roughness", &ctx.camera_shake_roughness, 0.01f, 0.0f, 2.0f);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Live Shake Offset: (%.2f px, %.2f px)", ctx.camera_shake_x,
                           ctx.camera_shake_y);
    }

    if (ImGui::CollapsingHeader("Lighting & Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient Color", ctx.general.ambient_color.data());
        ImGui::ColorEdit3("Skylight Color", ctx.general.skylight_color.data());
        ImGui::ColorEdit4("Clear Color", ctx.general.clear_color.data());
        ImGui::Checkbox("Clear Enabled", &ctx.general.clear_enabled);
        ImGui::SameLine();
        ImGui::Checkbox("HDR Accumulation", &ctx.general.hdr);
    }

    if (ImGui::CollapsingHeader("Bloom & HDR Post-Process", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Bloom Enabled", &ctx.general.bloom.enabled);
        ImGui::DragFloat("LDR Strength", &ctx.general.bloom.strength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("LDR Threshold", &ctx.general.bloom.threshold, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("HDR Scatter", &ctx.general.bloom.hdr_scatter, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("HDR Strength", &ctx.general.bloom.hdr_strength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("HDR Threshold", &ctx.general.bloom.hdr_threshold, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("HDR Feather", &ctx.general.bloom.hdr_feather, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("HDR Iterations", &ctx.general.bloom.hdr_iterations, 1.0f, 1.0f, 16.0f);
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
