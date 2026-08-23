#include "image_inspector.h"

#if DEBUG_BUILD

#include <string>

#include "image_layer.h"
#include "imgui.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "util/sokol_imgui.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {

const char* filenameFromPath(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1;
}

void showTextureSlot(int slot, const char* semantic, sg_view view, const std::string& path, int width = 0,
                     int height = 0) {
    ImGui::PushID(slot);
    ImGui::Text("g_Texture%d [%s]", slot, semantic && semantic[0] ? semantic : "Texture");

    if (view.id != SG_INVALID_ID) {
        const float max_width = 220.0f;
        const float max_height = 120.0f;
        float preview_width = max_width;
        float preview_height = max_height;
        if (width > 0 && height > 0) {
            const float aspect = (float)width / (float)height;
            if (aspect >= 1.0f) {
                preview_height = preview_width / aspect;
                if (preview_height > max_height) {
                    preview_height = max_height;
                    preview_width = preview_height * aspect;
                }
            } else {
                preview_width = preview_height * aspect;
            }
        }
        const float available = ImGui::GetContentRegionAvail().x;
        if (preview_width > available && available > 1.0f) {
            const float scale = available / preview_width;
            preview_width *= scale;
            preview_height *= scale;
        }
        ImGui::Image((ImTextureID)simgui_imtextureid(view), ImVec2(preview_width, preview_height));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "INVALID / not bound");
    }

    if (!path.empty()) {
        ImGui::TextDisabled("%s", filenameFromPath(path));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
    } else {
        ImGui::TextDisabled("runtime / no authored file path");
    }
    ImGui::PopID();
}

static constexpr const char* kBlendModeNames[] = {
    "Normal (Alpha)",      // 0
    "Multiply",            // 1
    "Multiply",            // 2
    "Color Burn",          // 3
    "Linear Burn",         // 4
    "Darker Color",        // 5
    "Lighten",             // 6
    "Screen",              // 7
    "Color Dodge",         // 8
    "Linear Dodge (Add)",  // 9
    "Lighter Color",       // 10
    "Overlay",             // 11
    "Soft Light",          // 12
    "Hard Light",          // 13
    "Vivid Light",         // 14
    "Linear Light",        // 15
    "Pin Light",           // 16
    "Hard Mix",            // 17
    "Difference",          // 18
    "Exclusion",           // 19
    "Subtract",            // 20
    "Divide",              // 21
    "Hue",                 // 22
    "Saturation",          // 23
    "Color",               // 24
    "Luminosity",          // 25
};

const char* blendModeName(int mode) {
    if (mode >= 0 && mode < static_cast<int>(IM_ARRAYSIZE(kBlendModeNames))) {
        return kBlendModeNames[mode];
    }
    if (mode == 31) return "Additive";
    return "Custom / Unknown";
}

bool showBlendModeSelector(const char* label, int& blend_mode) {
    bool changed = false;
    char current_label[64];
    snprintf(current_label, sizeof(current_label), "%d - %s", blend_mode, blendModeName(blend_mode));

    if (ImGui::BeginCombo(label, current_label)) {
        for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kBlendModeNames)); ++i) {
            const bool is_selected = (blend_mode == i);
            char item_name[64];
            snprintf(item_name, sizeof(item_name), "%d - %s", i, kBlendModeNames[i]);
            if (ImGui::Selectable(item_name, is_selected)) {
                blend_mode = i;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

}  // namespace

namespace Inspector {

void showImageLayerInspector(EngineContext& ctx, ImageLayer& il) {
    ImGui::Text("Type: %s",
                il.is_fullscreen ? "Fullscreen Post-Process" : (il.solid_layer ? "Solid Layer" : "Image Layer"));
    if (!il.is_fullscreen) {
        showBlendModeSelector("Blend Mode", il.color_blend_mode);
    }
    if (!il.path.empty()) ImGui::TextWrapped("Path: %s", il.path.c_str());

    if (il.img.id != SG_INVALID_ID) {
        sg_image_desc desc = sg_query_image_desc(il.img);
        showTextureSlot(0, "Base Albedo", il.cached_view, il.path, desc.width, desc.height);
    }

    SceneTreeNode* node =
        (il.scene_object_id != 0 && ctx.scene_tree) ? ctx.scene_tree->find(il.scene_object_id) : nullptr;

    float layer_scale[3] = {node ? node->scale[0] : il.scale[0], node ? node->scale[1] : il.scale[1],
                            node ? node->scale[2] : il.scale[2]};
    float layer_origin[3] = {node ? node->origin[0] : il.origin[0], node ? node->origin[1] : il.origin[1],
                             node ? node->origin[2] : il.origin[2]};
    float layer_rotation = node ? node->angles[2] : il.rotation;
    if (il.scene_object_id != 0 && ctx.scene_tree) {
        ctx.scene_tree->worldPosition(il.scene_object_id, layer_origin);
    }

    const float rendered_w = il.size[0] * layer_scale[0] * ctx.render_scale;
    const float rendered_h = il.size[1] * layer_scale[1] * ctx.render_scale;
    const float scene_h =
        ctx.scene_h > 0.0f ? ctx.scene_h : (ctx.renderer.view_height > 0.0f ? ctx.renderer.view_height : 2160.0f);
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, il.scene_object_id, layer_origin, il.parallax);
    const float rendered_x = ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - rendered_w * 0.5f;
    const float rendered_y =
        ctx.offset_y + (scene_h - (layer_origin[1] + camera_offset.y)) * ctx.render_scale - rendered_h * 0.5f;

    if (ImGui::CollapsingHeader("Resolution & Viewport Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
        sg_image_desc src_desc = (il.img.id != SG_INVALID_ID) ? sg_query_image_desc(il.img) : sg_image_desc{};
        ImGui::Text("Source Texture:   %d x %d px", src_desc.width, src_desc.height);
        ImGui::Text("Layer Author Size: %.0f x %.0f px", il.size[0], il.size[1]);
        ImGui::Text("Layer Rotation:   %.1f deg", layer_rotation);
        ImGui::Text("Rendered Bounds:  [x: %.1f, y: %.1f, w: %.1f, h: %.1f]", rendered_x, rendered_y, rendered_w,
                    rendered_h);
        ImGui::Text("Viewport Window:  %.0f x %.0f px", ctx.renderer.view_width, ctx.renderer.view_height);
        ImGui::Text("Design Canvas:    %.0f x %.0f px (Scale: %.3fx)", ctx.scene_w, ctx.scene_h, ctx.render_scale);
        ImGui::Text("Screen Padding:   (Offset X: %.1f px, Offset Y: %.1f px)", ctx.offset_x, ctx.offset_y);
    }

    const auto* video = ctx.asset_mgr.findVideoTexture(il.img);
    if (!video && !il.path.empty()) video = ctx.asset_mgr.findVideoTexture(il.path);
    if (video && video->decoder) {
        if (ImGui::CollapsingHeader("Video Stream & Hardware Acceleration", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& dec = *video->decoder;
            const auto& m = dec.getMetrics();
            const auto& s = dec.getStats();
            const auto& t = dec.getTiming();
            const auto& gpu = GpuDeviceManager::instance().getSelectedGpu();

            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Decode Mode:  %s",
                               dec.isZeroCopy() ? "Zero-Copy VA-API (Hardware VRAM)" : "Software Fallback (FFmpeg)");
            ImGui::Text("Codec / Format: %s (%s)", dec.codecName().empty() ? "h264" : dec.codecName().c_str(),
                        dec.containerName().empty() ? "mp4" : dec.containerName().c_str());
            ImGui::Text("Stream Native:  %u x %u @ %.2f FPS", dec.width(), dec.height(), dec.fps());
            ImGui::Text("Surface / DRM:  NV12 (DRM_FORMAT_NV12) -> VK_FORMAT_G8_B8R8_2PLANE_420_UNORM");
            ImGui::Text("Active GPU:     [%u] %s", gpu.index, gpu.name.empty() ? "Default" : gpu.name.c_str());
            ImGui::Text("DRM Render:     %s", gpu.drm_render_node.empty() ? "N/A" : gpu.drm_render_node.c_str());

            ImGui::Separator();
            ImGui::Text("Decoded Frames: %llu (HW: %llu)", (unsigned long long)s.frames_decoded,
                        (unsigned long long)m.vaapi_frames_decoded);
            const uint64_t total_cache = m.import_cache_hits + m.import_cache_misses;
            const double hit_rate =
                total_cache > 0 ? (100.0 * (double)m.import_cache_hits / (double)total_cache) : 100.0;
            ImGui::Text("DMA-BUF Cache:  %llu Hits / %llu Misses (%.1f%% Hit Rate)",
                        (unsigned long long)m.import_cache_hits, (unsigned long long)m.import_cache_misses, hit_rate);
            ImGui::Text("CPU Copies:     %llu B (sws_scale: %llu)", (unsigned long long)m.cpu_rgba_bytes,
                        (unsigned long long)m.sws_scale_calls);
            ImGui::Text("Demux Latency:  %.3f ms | Decode Submit: %.3f ms", t.demux_cpu_ms, t.decode_submit_cpu_ms);
            ImGui::Text("VA Sync Latency:%.3f ms | Sched Jitter:  %.3f ms", t.va_sync_cpu_ms, t.scheduler_cpu_ms);
        }
    }

    ImGui::Separator();
    ImGui::DragFloat2("Size", (float*)il.size, 1.0f, 1.0f, 16384.0f);
    ImGui::ColorEdit4("Tint", il.tint);
}

}  // namespace Inspector

#endif  // DEBUG_BUILD
