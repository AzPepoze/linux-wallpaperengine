#include "layer_inspector.h"

#include <string>

#include "effect_inspector.h"
#include "imgui.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "util/sokol_imgui.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/layers/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle_layer.h"
#include "wallpaper/2d/particles/particle_system.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace Inspector {

static void showBaseInspector(::Layer& layer) {
    bool visible = layer.is_visible();
    if (ImGui::Checkbox("Visible", &visible)) {
        layer.setVisible(visible);
    }
    ImGui::SameLine();
    bool solo = layer.solo;
    if (ImGui::Checkbox("Solo", &solo)) {
        layer.setSolo(solo);
    }
}

static void showEffectsInspector(EngineContext& ctx, ::Layer& layer) {
    if (layer.effects.empty()) return;
    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)layer.effects.size(); i++) showEffect(ctx, *layer.effects[i], i);
    }
}

static const char* filenameFromPath(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1;
}

static void showParticleTextureSlot(int slot, const char* semantic, sg_view view, const std::string& path,
                                    int width = 0, int height = 0) {
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

static void showParticleMaterial(::ParticleSystem& ps) {
    if (!ps.material_pass) {
        ImGui::TextDisabled("No authored material pass");
        return;
    }

    ::ShaderPass& pass = *ps.material_pass;
    if (!ps.config.material_path.empty()) {
        ImGui::TextWrapped("Material: %s", ps.config.material_path.c_str());
    }
    ImGui::Text("Shader: %s", pass.shader_name.empty() ? "(none)" : pass.shader_name.c_str());

    if (ImGui::TreeNodeEx("Material Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* texture0_label = "Albedo / Base";
        const auto texture0_label_it = pass.texture_labels.find(0);
        if (texture0_label_it != pass.texture_labels.end()) texture0_label = texture0_label_it->second.c_str();
        showParticleTextureSlot(0, texture0_label, pass.pass_textures.texture0_view, pass.pass_textures.texture0_path,
                                ps.texture_width, ps.texture_height);

        for (int i = 0; i < (int)pass.pass_textures.cached_views.size(); ++i) {
            const int shader_slot = i + 1;
            const auto label_it = pass.texture_labels.find(shader_slot);
            const char* label = label_it != pass.texture_labels.end() ? label_it->second.c_str() : "Extra";
            const std::string path =
                i < (int)pass.pass_textures.texture_paths.size() ? pass.pass_textures.texture_paths[i] : std::string();
            showParticleTextureSlot(shader_slot, label, pass.pass_textures.cached_views[i], path);
        }

        if (!pass.render_texture_bindings.empty()) {
            ImGui::SeparatorText("Runtime texture bindings");
            for (const auto& binding : pass.render_texture_bindings) {
                if (binding.second == "_rt_FullFrameBuffer" && ps.sceneColorView().id != SG_INVALID_ID) {
                    showParticleTextureSlot(binding.first, "Scene Color / Refraction", ps.sceneColorView(),
                                            std::string(), (int)ps.scene_w, (int)ps.scene_h);
                } else {
                    ImGui::Text("g_Texture%d <- %s", binding.first, binding.second.c_str());
                }
            }
        }
        ImGui::TreePop();
    }

    if (!pass.uniforms.empty() && ImGui::TreeNodeEx("Shader Uniforms", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& [name, values] : pass.uniforms) {
            if (values.empty()) continue;
            ImGui::PushID(name.c_str());
            if (values.size() == 1) {
                ImGui::DragFloat(name.c_str(), &values[0], 0.01f);
            } else if (values.size() == 2) {
                ImGui::DragFloat2(name.c_str(), values.data(), 0.01f);
            } else if (values.size() == 3) {
                ImGui::DragFloat3(name.c_str(), values.data(), 0.01f);
            } else if (values.size() >= 4) {
                if (name.find("color") != std::string::npos || name.find("Color") != std::string::npos ||
                    name.find("tint") != std::string::npos || name.find("Tint") != std::string::npos) {
                    ImGui::ColorEdit4(name.c_str(), values.data());
                } else {
                    ImGui::DragFloat4(name.c_str(), values.data(), 0.01f);
                }
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (!pass.combos.empty() && ImGui::TreeNode("Shader Combos")) {
        for (const auto& combo : pass.combos) ImGui::BulletText("%s = %d", combo.first.c_str(), combo.second);
        ImGui::TreePop();
    }
}

static void showSpriteSheetInfo(::ParticleSystem& ps) {
    const bool detected = ps.spritesheet_frames > 1 && ps.spritesheet_cols > 0 && ps.spritesheet_rows > 0;
    if (!detected) {
        ImGui::Text("Sprite Sheet: Not detected");
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Sprite Sheet: Detected");
    ImGui::Text("Atlas: %dx%d", ps.texture_width, ps.texture_height);
    ImGui::Text("Grid: %d x %d", ps.spritesheet_cols, ps.spritesheet_rows);
    ImGui::Text("Frames: %d", ps.spritesheet_frames);
    ImGui::Text("Animation Mode: %s", ps.config.animation_mode.c_str());

    if (ps.config.animation_mode == "randomframe") {
        ImGui::TextDisabled("Random frame: each particle keeps one atlas frame.");
    } else if (ps.config.animation_mode == "sequence" || ps.config.animation_mode == "once") {
        ImGui::DragFloat("Sequence Multiplier", &ps.config.sequence_multiplier, 0.01f, 0.0f, 100.0f);
        ImGui::TextDisabled("Sequence: sprite frames advance over particle lifetime.");
    }
}

static void showParticleSystem(::ParticleSystem& ps) {
    ImGui::Text("Active Particles: %d / %d", (int)ps.particles.size(), ps.max_particles);
    if (!ps.config_path.empty()) ImGui::TextWrapped("Config: %s", ps.config_path.c_str());
    ImGui::Checkbox("Additive Blending", &ps.is_additive);
    ImGui::Text("Children: %d", (int)ps.children.size());

    ImGui::SeparatorText("Particle Material");
    showParticleMaterial(ps);

    ImGui::SeparatorText("Sprite Detection");
    showSpriteSheetInfo(ps);
}

void showGlobalSettings(EngineContext& ctx) {
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

void showLayer(EngineContext& ctx, ::Layer& layer) {
    showBaseInspector(layer);

    if (auto* il = dynamic_cast<::ImageLayer*>(&layer)) {
        ImGui::Text("Type: %s",
                    il->is_fullscreen ? "Fullscreen Post-Process" : (il->solid_layer ? "Solid Layer" : "Image Layer"));
        if (!il->is_fullscreen) {
            const char* blend_names[] = {
                "0 - Normal (Alpha)", "1 - Multiply",   "2 - Multiply",    "3 - Color Burn",  "4 - Linear Burn",
                "5 - Darker Color",   "6 - Lighten",    "7 - Screen",      "8 - Color Dodge", "9 - Linear Dodge (Add)",
                "10 - Lighter Color", "11 - Overlay",   "12 - Soft Light", "13 - Hard Light", "14 - Vivid Light",
                "15 - Linear Light",  "16 - Pin Light", "17 - Hard Mix",   "18 - Difference", "19 - Exclusion",
                "20 - Subtract",      "21 - Divide",    "22 - Hue",        "23 - Saturation", "24 - Color",
                "25 - Luminosity"};
            int current_mode = il->color_blend_mode;
            if (current_mode >= 0 && current_mode < (int)IM_ARRAYSIZE(blend_names)) {
                if (ImGui::Combo("Blend Mode", &current_mode, blend_names, IM_ARRAYSIZE(blend_names))) {
                    il->color_blend_mode = current_mode;
                }
            } else {
                ImGui::DragInt("Blend Mode", &il->color_blend_mode, 1, 0, 30);
            }
        }
        if (!il->path.empty()) ImGui::TextWrapped("Path: %s", il->path.c_str());

        if (il->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(il->img);
            showParticleTextureSlot(0, "Base Albedo", il->cached_view, il->path, desc.width, desc.height);
        }

        SceneTreeNode* node =
            (il->scene_object_id != 0 && ctx.scene_tree) ? ctx.scene_tree->find(il->scene_object_id) : nullptr;

        float layer_scale[3] = {node ? node->scale[0] : il->scale[0], node ? node->scale[1] : il->scale[1],
                                node ? node->scale[2] : il->scale[2]};
        float layer_origin[3] = {node ? node->origin[0] : il->origin[0], node ? node->origin[1] : il->origin[1],
                                 node ? node->origin[2] : il->origin[2]};
        float layer_rotation = node ? node->angles[2] : il->rotation;
        if (il->scene_object_id != 0 && ctx.scene_tree) {
            ctx.scene_tree->worldPosition(il->scene_object_id, layer_origin);
        }

        const float rendered_w = il->size[0] * layer_scale[0] * ctx.render_scale;
        const float rendered_h = il->size[1] * layer_scale[1] * ctx.render_scale;
        const float scene_h =
            ctx.scene_h > 0.0f ? ctx.scene_h : (ctx.renderer.view_height > 0.0f ? ctx.renderer.view_height : 2160.0f);
        const parallax_offset_t camera_offset =
            parallax_layer_offset(ctx, il->scene_object_id, layer_origin, il->parallax);
        const float rendered_x =
            ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - rendered_w * 0.5f;
        const float rendered_y =
            ctx.offset_y + (scene_h - (layer_origin[1] + camera_offset.y)) * ctx.render_scale - rendered_h * 0.5f;

        if (ImGui::CollapsingHeader("Resolution & Viewport Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
            sg_image_desc src_desc = (il->img.id != SG_INVALID_ID) ? sg_query_image_desc(il->img) : sg_image_desc{};
            ImGui::Text("Source Texture:   %d x %d px", src_desc.width, src_desc.height);
            ImGui::Text("Layer Author Size: %.0f x %.0f px", il->size[0], il->size[1]);
            ImGui::Text("Layer Rotation:   %.1f deg", layer_rotation);
            ImGui::Text("Rendered Bounds:  [x: %.1f, y: %.1f, w: %.1f, h: %.1f]", rendered_x, rendered_y, rendered_w,
                        rendered_h);
            ImGui::Text("Viewport Window:  %.0f x %.0f px", ctx.renderer.view_width, ctx.renderer.view_height);
            ImGui::Text("Design Canvas:    %.0f x %.0f px (Scale: %.3fx)", ctx.scene_w, ctx.scene_h, ctx.render_scale);
            ImGui::Text("Screen Padding:   (Offset X: %.1f px, Offset Y: %.1f px)", ctx.offset_x, ctx.offset_y);
        }

        const auto* video = ctx.asset_mgr.findVideoTexture(il->img);
        if (!video && !il->path.empty()) video = ctx.asset_mgr.findVideoTexture(il->path);
        if (video && video->decoder) {
            if (ImGui::CollapsingHeader("Video Stream & Hardware Acceleration", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& dec = *video->decoder;
                const auto& m = dec.getMetrics();
                const auto& s = dec.getStats();
                const auto& t = dec.getTiming();
                const auto& gpu = GpuDeviceManager::instance().getSelectedGpu();

                ImGui::TextColored(
                    ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Decode Mode:  %s",
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
                            (unsigned long long)m.import_cache_hits, (unsigned long long)m.import_cache_misses,
                            hit_rate);
                ImGui::Text("CPU Copies:     %llu B (sws_scale: %llu)", (unsigned long long)m.cpu_rgba_bytes,
                            (unsigned long long)m.sws_scale_calls);
                ImGui::Text("Demux Latency:  %.3f ms | Decode Submit: %.3f ms", t.demux_cpu_ms, t.decode_submit_cpu_ms);
                ImGui::Text("VA Sync Latency:%.3f ms | Sched Jitter:  %.3f ms", t.va_sync_cpu_ms, t.scheduler_cpu_ms);
            }
        }

        ImGui::Separator();
        float* pos_ptr = node ? node->origin.data() : (float*)il->origin;
        if (ImGui::DragFloat3("Position", pos_ptr, 1.0f)) {
            if (node) {
                il->origin[0] = node->origin[0];
                il->origin[1] = node->origin[1];
                il->origin[2] = node->origin[2];
            }
        }

        float* scale_ptr = node ? node->scale.data() : (float*)il->scale;
        if (ImGui::DragFloat3("Scale", scale_ptr, 0.01f, 0.001f, 100.0f)) {
            if (node) {
                il->scale[0] = node->scale[0];
                il->scale[1] = node->scale[1];
                il->scale[2] = node->scale[2];
            }
        }

        ImGui::DragFloat2("Size", (float*)il->size, 1.0f, 1.0f, 16384.0f);

        float rot = node ? node->angles[2] : il->rotation;
        if (ImGui::DragFloat("Rotation", &rot, 1.0f, -360.0f, 360.0f)) {
            if (node) node->angles[2] = rot;
            il->rotation = rot;
        }

        ImGui::ColorEdit4("Tint", il->tint);

        float* parallax_ptr = node ? node->parallax_depth.data() : (float*)il->parallax;
        if (ImGui::DragFloat2("Parallax Depth", parallax_ptr, 0.01f, -10.0f, 10.0f)) {
            if (node) {
                il->parallax[0] = node->parallax_depth[0];
                il->parallax[1] = node->parallax_depth[1];
            }
        }
    } else if (auto* pl = dynamic_cast<::ParticleLayer*>(&layer)) {
        ImGui::Text("Type: Particle System");
        ImGui::TextDisabled("Class: ParticleLayer");
        if (!pl->path.empty()) ImGui::TextWrapped("Path: %s", pl->path.c_str());

        ImGui::Separator();
        if (pl->ps) showParticleSystem(*pl->ps);

        ImGui::Separator();
        SceneTreeNode* node =
            (pl->scene_object_id != 0 && ctx.scene_tree) ? ctx.scene_tree->find(pl->scene_object_id) : nullptr;

        float* pos_ptr = node ? node->origin.data() : (float*)pl->origin;
        if (ImGui::DragFloat3("Position", pos_ptr, 1.0f)) {
            if (node) {
                pl->origin[0] = node->origin[0];
                pl->origin[1] = node->origin[1];
                pl->origin[2] = node->origin[2];
            }
        }

        float* scale_ptr = node ? node->scale.data() : (float*)pl->scale;
        if (ImGui::DragFloat3("Scale", scale_ptr, 0.01f, 0.001f, 100.0f)) {
            if (node) {
                pl->scale[0] = node->scale[0];
                pl->scale[1] = node->scale[1];
                pl->scale[2] = node->scale[2];
            }
        }

        float rot = node ? node->angles[2] : pl->rotation;
        if (ImGui::DragFloat("Rotation", &rot, 1.0f, -360.0f, 360.0f)) {
            if (node) node->angles[2] = rot;
            pl->rotation = rot;
        }

        float* parallax_ptr = node ? node->parallax_depth.data() : (float*)pl->parallax;
        if (ImGui::DragFloat2("Parallax Depth", parallax_ptr, 0.01f, -10.0f, 10.0f)) {
            if (node) {
                pl->parallax[0] = node->parallax_depth[0];
                pl->parallax[1] = node->parallax_depth[1];
            }
        }
    }

    showEffectsInspector(ctx, layer);
}

}  // namespace Inspector
