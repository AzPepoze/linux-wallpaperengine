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

void showLayer(EngineContext& ctx, ::Layer& layer) {
    showBaseInspector(layer);

    if (auto* il = dynamic_cast<::ImageLayer*>(&layer)) {
        ImGui::Text("Type: Image");
        ImGui::TextDisabled("Class: ImageLayer");
        if (!il->path.empty()) ImGui::Text("Path: %s", il->path.c_str());

        ImGui::Separator();
        ImGui::DragFloat3("Position", (float*)il->origin, 1.0f);
        ImGui::DragFloat3("Scale", (float*)il->scale, 0.01f);
        ImGui::DragFloat2("Size", (float*)il->size, 1.0f);
        ImGui::DragFloat("Rotation", &il->rotation, 1.0f, 0, 360);
        ImGui::ColorEdit4("Tint", il->tint);
        ImGui::DragFloat2("Parallax", (float*)il->parallax, 0.01f, -10, 10);
    } else if (auto* pl = dynamic_cast<::ParticleLayer*>(&layer)) {
        ImGui::Text("Type: Particle System");
        ImGui::TextDisabled("Class: ParticleLayer");
        if (!pl->path.empty()) ImGui::Text("Path: %s", pl->path.c_str());

        ImGui::Separator();
        if (pl->ps) showParticleSystem(*pl->ps);

        ImGui::Separator();
        ImGui::DragFloat3("Position", (float*)pl->origin, 1.0f);
        ImGui::DragFloat2("Parallax", (float*)pl->parallax, 0.01f, -10, 10);
    }

    showEffectsInspector(ctx, layer);
}

}  // namespace Inspector
