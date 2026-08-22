#include "layer_inspector.h"

#include <string>

#include "effect_inspector.h"
#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "util/sokol_imgui.h"
#include "wallpaper/scene/2d/effects/effect.h"
#include "wallpaper/scene/2d/layers/image_layer.h"
#include "wallpaper/scene/2d/layers/layer.h"
#include "wallpaper/scene/2d/layers/particle_layer.h"
#include "wallpaper/scene/2d/particles/particle_system.h"

namespace Inspector {

static void showBaseInspector(::Layer& layer) {
    ImGui::Checkbox("Visible", &layer.visible);
    ImGui::SameLine();
    ImGui::Checkbox("Solo", &layer.solo);
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

    if (!pass.combos.empty() && ImGui::TreeNode("Shader Combos")) {
        for (const auto& combo : pass.combos) ImGui::BulletText("%s = %d", combo.first.c_str(), combo.second);
        ImGui::TreePop();
    }
}

static void showSpriteSheetInfo(const ::ParticleSystem& ps) {
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
        ImGui::Text("Sequence Multiplier: %.3f", ps.config.sequence_multiplier);
        ImGui::TextDisabled("Sequence: sprite frames advance over particle lifetime.");
    }
}

static void showParticleSystem(::ParticleSystem& ps) {
    ImGui::Text("Active Particles: %d / %d", (int)ps.particles.size(), ps.max_particles);
    if (!ps.config_path.empty()) ImGui::TextWrapped("Config: %s", ps.config_path.c_str());
    ImGui::Text("Blending: %s", ps.is_additive ? "Additive" : "Alpha");
    ImGui::Text("Children: %d", (int)ps.children.size());

    ImGui::SeparatorText("Particle Material");
    showParticleMaterial(ps);

    ImGui::SeparatorText("Sprite Detection");
    showSpriteSheetInfo(ps);
}

static const char* blendModeName(int mode) {
    switch (mode) {
        case 0: return "Normal (Alpha)";
        case 1: return "Multiply";
        case 2: return "Multiply";
        case 3: return "Color Burn";
        case 4: return "Linear Burn";
        case 5: return "Darker Color";
        case 6: return "Lighten";
        case 7: return "Screen";
        case 8: return "Color Dodge";
        case 9: return "Linear Dodge (Add)";
        case 10: return "Lighter Color";
        case 11: return "Overlay";
        case 12: return "Soft Light";
        case 13: return "Hard Light";
        case 14: return "Vivid Light";
        case 15: return "Linear Light";
        case 16: return "Pin Light";
        case 17: return "Hard Mix";
        case 18: return "Difference";
        case 19: return "Exclusion";
        case 20: return "Subtract";
        case 21: return "Divide";
        case 22: return "Hue";
        case 23: return "Saturation";
        case 24: return "Color";
        case 25: return "Luminosity";
        case 31: return "Additive";
        default: return "Custom / Unknown";
    }
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
    ImGui::Text("FPS: %.1f | Frame Time: %.2f ms", ImGui::GetIO().Framerate, 1000.0f / (ImGui::GetIO().Framerate > 0.0f ? ImGui::GetIO().Framerate : 60.0f));

    if (ImGui::CollapsingHeader("Camera & Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Eye Position", ctx.camera.eye.data(), 0.1f);
        ImGui::DragFloat3("Center LookAt", ctx.camera.center.data(), 0.1f);
        ImGui::DragFloat3("Up Vector", ctx.camera.up.data(), 0.1f);
        ImGui::DragFloat("FOV", &ctx.general.fov, 0.5f, 1.0f, 179.0f);
        ImGui::DragFloat("Near Z", &ctx.general.near_z, 0.001f, 0.001f, 10.0f);
        ImGui::DragFloat("Far Z", &ctx.general.far_z, 10.0f, 10.0f, 100000.0f);
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
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Live Shake Offset: (%.2f px, %.2f px)",
                           ctx.camera_shake_x, ctx.camera_shake_y);
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
        ImGui::DragFloat("Strength", &ctx.general.bloom.strength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Threshold", &ctx.general.bloom.threshold, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("HDR Scatter", &ctx.general.bloom.hdr_scatter, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("HDR Strength", &ctx.general.bloom.hdr_strength, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("HDR Threshold", &ctx.general.bloom.hdr_threshold, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("HDR Feather", &ctx.general.bloom.hdr_feather, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("HDR Iterations", &ctx.general.bloom.hdr_iterations, 1.0f, 1.0f, 16.0f);
    }
}

void showLayer(EngineContext& ctx, ::Layer& layer) {
    showBaseInspector(layer);

    if (auto* il = dynamic_cast<::ImageLayer*>(&layer)) {
        ImGui::Text("Type: %s", il->is_fullscreen ? "Fullscreen Post-Process" : (il->solid_layer ? "Solid Layer" : "Image Layer"));
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Blend Mode: %d - %s", il->color_blend_mode,
                           blendModeName(il->color_blend_mode));
        if (!il->path.empty()) ImGui::TextWrapped("Path: %s", il->path.c_str());

        if (il->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(il->img);
            showParticleTextureSlot(0, "Base Albedo", il->cached_view, il->path, desc.width, desc.height);
        }

        ImGui::Separator();
        ImGui::DragFloat3("Position", (float*)il->origin, 1.0f);
        ImGui::DragFloat3("Scale", (float*)il->scale, 0.01f);
        ImGui::DragFloat2("Size", (float*)il->size, 1.0f);
        ImGui::DragFloat("Rotation", &il->rotation, 1.0f, 0, 360);
        ImGui::ColorEdit4("Tint", il->tint);
        ImGui::DragFloat2("Parallax Depth", (float*)il->parallax, 0.01f, -10, 10);
    } else if (auto* pl = dynamic_cast<::ParticleLayer*>(&layer)) {
        ImGui::Text("Type: Particle System");
        ImGui::TextDisabled("Class: ParticleLayer");
        if (!pl->path.empty()) ImGui::TextWrapped("Path: %s", pl->path.c_str());

        ImGui::Separator();
        if (pl->ps) showParticleSystem(*pl->ps);

        ImGui::Separator();
        ImGui::DragFloat3("Position", (float*)pl->origin, 1.0f);
        ImGui::DragFloat2("Parallax Depth", (float*)pl->parallax, 0.01f, -10, 10);
    }

    showEffectsInspector(ctx, layer);
}

}  // namespace Inspector
