#include "particle_inspector.h"

#if DEBUG_BUILD

#include <string>

#include "imgui.h"
#include "particle_layer.h"
#include "particle_system.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/shader/shader_compiler.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "util/sokol_imgui.h"

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

void showParticleMaterial(ParticleSystem& ps) {
    if (!ps.material_pass) {
        ImGui::TextDisabled("No authored material pass");
        return;
    }

    ShaderPass& pass = *ps.material_pass;
    if (!ps.config.material_path.empty()) {
        ImGui::TextWrapped("Material: %s", ps.config.material_path.c_str());
    }
    ImGui::Text("Shader: %s", pass.shader_name.empty() ? "(none)" : pass.shader_name.c_str());

    if (ImGui::TreeNodeEx("Material Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* texture0_label = "Albedo / Base";
        const auto texture0_label_it = pass.texture_labels.find(0);
        if (texture0_label_it != pass.texture_labels.end()) texture0_label = texture0_label_it->second.c_str();
        showTextureSlot(0, texture0_label, pass.pass_textures.texture0_view, pass.pass_textures.texture0_path,
                        ps.texture_width, ps.texture_height);

        for (int i = 0; i < (int)pass.pass_textures.cached_views.size(); ++i) {
            const int shader_slot = i + 1;
            const auto label_it = pass.texture_labels.find(shader_slot);
            const char* label = label_it != pass.texture_labels.end() ? label_it->second.c_str() : "Extra";
            const std::string path =
                i < (int)pass.pass_textures.texture_paths.size() ? pass.pass_textures.texture_paths[i] : std::string();
            showTextureSlot(shader_slot, label, pass.pass_textures.cached_views[i], path);
        }

        if (!pass.render_texture_bindings.empty()) {
            ImGui::SeparatorText("Runtime texture bindings");
            for (const auto& binding : pass.render_texture_bindings) {
                if (binding.second == "_rt_FullFrameBuffer" && ps.sceneColorView().id != SG_INVALID_ID) {
                    showTextureSlot(binding.first, "Scene Color / Refraction", ps.sceneColorView(), std::string(),
                                    (int)ps.scene_w, (int)ps.scene_h);
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

void showSpriteSheetInfo(ParticleSystem& ps) {
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

void showParticleSystemDetails(ParticleSystem& ps) {
    ImGui::Text("Active Particles: %d / %d", (int)ps.particles.size(), ps.max_particles);
    if (!ps.config_path.empty()) ImGui::TextWrapped("Config: %s", ps.config_path.c_str());

    if (ImGui::Checkbox("Additive Blending", &ps.is_additive)) {
        if (ps.material_pass && ps.material_pass->compiled.shader.id != SG_INVALID_ID) {
            ps.material_pass->compiled.pipeline = ShaderCompiler::makePipeline(
                ps.material_pass->compiled.shader, ps.material_pass->compiled.vertex_layout,
                ps.is_additive ? ShaderBlendMode::Additive : ShaderBlendMode::Alpha);
        }
    }

    ImGui::SliderFloat("Alpha Override", &ps.override_alpha, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Rate / Count Multiplier", &ps.override_rate, 0.0f, 20.0f, "%.2f");

    if (ImGui::TreeNode("Emitter / Spawn Controls")) {
        for (size_t i = 0; i < ps.config.emitters.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Emitter #%zu (%s)", i, ps.config.emitters[i].type.c_str());
            ImGui::DragFloat("Authored Rate", &ps.config.emitters[i].rate, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat3("Origin", ps.config.emitters[i].origin, 1.0f);
            ImGui::PopID();
        }
        for (size_t i = 0; i < ps.config.initializers.size(); ++i) {
            auto& init = ps.config.initializers[i];
            ImGui::PushID(static_cast<int>(100 + i));
            if (init.type == "lifetimerandom") {
                ImGui::DragFloatRange2("Lifetime (s)", &init.minimum_scalar, &init.maximum_scalar, 0.05f, 0.01f, 30.0f);
            } else if (init.type == "sizerandom") {
                ImGui::DragFloatRange2("Size", &init.minimum_scalar, &init.maximum_scalar, 1.0f, 1.0f, 5000.0f);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::Text("Children: %d", (int)ps.children.size());
    if (!ps.children.empty() && ImGui::TreeNode("Child Systems")) {
        for (size_t i = 0; i < ps.children.size(); ++i) {
            ImGui::PushID(static_cast<int>(200 + i));
            if (ps.children[i] && ImGui::TreeNode(ps.children[i]->name.empty() ? ("Child #" + std::to_string(i)).c_str()
                                                                               : ps.children[i]->name.c_str())) {
                showParticleSystemDetails(*ps.children[i]);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::SeparatorText("Particle Material");
    showParticleMaterial(ps);

    ImGui::SeparatorText("Sprite Detection");
    showSpriteSheetInfo(ps);
}

}  // namespace

namespace Inspector {

void showParticleLayerInspector(EngineContext& /*ctx*/, ParticleLayer& pl) {
    ImGui::Text("Type: Particle System");
    ImGui::TextDisabled("Class: ParticleLayer");
    if (!pl.path.empty()) ImGui::TextWrapped("Path: %s", pl.path.c_str());

    ImGui::Separator();
    if (pl.ps) showParticleSystemDetails(*pl.ps);
}

}  // namespace Inspector

#endif  // DEBUG_BUILD
