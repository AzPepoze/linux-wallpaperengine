#ifndef SHADER_PASS_H
#define SHADER_PASS_H

#include <cjson/cJSON.h>

#include <future>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "effect_geometry.h"
#include "pass_textures.h"
#include "shared/graphics/gfx_resource.h"
#include "shared/graphics/pointer_state.h"
#include "shared/graphics/render.h"
#include "shared/graphics/shader/shader_compiler.h"
#include "sokol_gfx.h"
#include "wallpaper/2d/animation_curve.h"
#include "wallpaper/2d/effects/effect_parser.h"

class EngineContext;

struct ShaderSourceSet {
    std::string raw_vs;
    std::string raw_fs;
    std::string processed_vs;
    std::string processed_fs;
    std::string full_vs;
    std::string full_fs;
};

struct PreparedShader {
    ShaderSourceSet sources;
    std::vector<ShaderUniformConfig> shader_uniforms;
    int texture_count = 0;
};

class ShaderPass {
   public:
    std::string shader_name;
    CompiledShader compiled;
    PassTextures pass_textures;
    std::map<std::string, std::vector<float>> uniforms;
    std::map<std::string, int> combos;
    std::map<int, std::string> texture_labels;
    unsigned int white_default_slots = 0;
    bool display_resolution_safe = false;
    // The shader reads a built-in that changes without any uniform changing (time, pointer, parallax, audio), so its
    // output can differ every frame. Until the sources are known it is assumed to vary.
    bool frame_varying = true;
    bool enabled = true;
    bool show_files = false;
    bool is_fullscreen_quad = false;
    bool geometry_classified = false;
    std::string render_target;
    float render_scale = 1.0f;
    std::map<int, std::string> render_texture_bindings;
    int effect_index = 0;
    int pass_index = 0;
    std::string effect_file;

    std::map<std::string, std::vector<float>> base_uniforms;
    std::map<std::string, std::vector<float>> pass_uniforms;
    std::map<std::string, std::vector<float>> inst_uniforms;
    std::map<std::string, wallpaper_engine::AnimationCurve> animated_uniforms;
    std::map<std::string, int> base_combos;
    std::map<std::string, int> pass_combos;
    std::map<std::string, int> inst_combos;

    ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx);
    ~ShaderPass() {
        if (pending_.valid()) pending_.wait();
    }

    void init(EngineContext& ctx);
    void initAsync(EngineContext& ctx);
    void completeInit(EngineContext& ctx);
    void rebuildWithDebugMode(int mode, EngineContext& ctx);
    // Auto-resolve depth map (g_Texture1) from the layer's .tex container (index 1)
    bool resolveDepth(const char* source_tex_path, EngineContext& ctx);

    uint64_t current_frame = 0;
    void applyCompiledUniforms() {
        for (const auto& block : compiled.custom_uniform_blocks) {
            if (block.slot < 0 || block.uniform_names.empty()) continue;

            std::vector<float> packed(block.uniform_names.size() * 4, 0.0f);
            for (size_t i = 0; i < block.uniform_names.size(); ++i) {
                const std::string& uniform_name = block.uniform_names[i];
                auto value = uniforms.find(uniform_name);
                if (value == uniforms.end()) continue;
                for (size_t component = 0; component < value->second.size() && component < 4; ++component) {
                    packed[i * 4 + component] = value->second[component];
                }
            }

            sg_range range = {.ptr = packed.data(), .size = packed.size() * sizeof(float)};
            sg_apply_uniforms(block.slot, &range);
        }
        applyAudioSpectrumBlocks();
    }

    render_effect_pass_t getRenderPass(uint64_t frame_index = 0, float time = 0.0f) {
        current_frame = frame_index;
        updateAnimatedUniforms(time);
        if (!geometry_classified) {
            is_fullscreen_quad = compiled.vertex_layout == ShaderVertexLayout::Sprite2D &&
                                 effectShaderUsesClipSpaceGeometry(stored_vs_source, shader_name.c_str());
            geometry_classified = true;
        }

        render_effect_pass_t r = {};
        r.enabled = enabled;
        r.pipeline = compiled.pipeline;
        r.shader_name = shader_name.c_str();
        r.extra_views = pass_textures.cached_views.data();
        r.num_extra_views = pass_textures.cached_views.size();
        r.override_views = nullptr;
        r.num_override_views = 0;
        r.apply_custom_uniforms = [](void* ud) { static_cast<ShaderPass*>(ud)->applyCompiledUniforms(); };
        r.user_data = this;
        r.is_fullscreen_quad = is_fullscreen_quad;
        const auto repeat = combos.find("REPEAT");
        r.repeat_effect_input = repeat != combos.end() && repeat->second != 0;
        r.white_default_slots = white_default_slots;
        return r;
    }

    // Material constants by authored (material key) or shader uniform name. A value set by a script replaces the
    // constant's keyframe animation for good.
    bool setMaterialConstant(const std::string& name, const std::vector<float>& values);
    const std::vector<float>* materialConstant(const std::string& name) const;

    int debug_view_mode = 0;
    int debug_step = 0;  // 0=full shader, 1+ = forced texture output (bypasses main logic)

   private:
    bool prepare(EngineContext& ctx, bool warm_cache);
    void finish(EngineContext& ctx);
    std::shared_ptr<PreparedShader> prepared_;
    std::shared_future<bool> pending_;
    void resolveUniforms(const std::vector<ShaderUniformConfig>& shader_uniforms);
    std::string buildComboDefines(const ShaderSourceSet& sources) const;
    void warnAboutMissingTextures() const;
    void registerDiagnostics(const ShaderSourceSet& sources,
                             const std::vector<ShaderUniformConfig>& shader_uniforms) const;
    void applyAudioSpectrumBlocks();
    void updateAnimatedUniforms(float time);
    bool resolveMaterialName(const std::string& name, std::string& resolved) const;
    std::vector<ShaderUniformConfig> shader_uniform_configs_;
    std::string stored_vs_source;
    std::string stored_fs_source;
    std::map<std::string, wallpaper_engine::AnimationCurve> resolved_animations;
};

#endif  // SHADER_PASS_H
