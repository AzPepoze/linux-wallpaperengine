#ifndef EFFECT_H
#define EFFECT_H

#include <cjson/cJSON.h>

#include <map>
#include <string>
#include <vector>

#include "core/gfx_resource.h"
#include "effect_geometry.h"
#include "pass_textures.h"
#include "render/render.h"
#include "render/shader/shader_compiler.h"
#include "sokol_gfx.h"

class EngineContext;

class ShaderPass {
   public:
    std::string shader_name;
    CompiledShader compiled;
    PassTextures pass_textures;
    cJSON* constant_values = nullptr;
    std::map<std::string, std::vector<float>> uniforms;
    std::map<std::string, int> combos;
    std::map<int, std::string> texture_labels;
    bool enabled = true;
    bool show_files = false;
    bool is_fullscreen_quad = false;
    bool geometry_classified = false;
    std::string render_target;
    float render_scale = 1.0f;
    std::map<int, std::string> render_texture_bindings;

    ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx);
    ~ShaderPass();

    void init(EngineContext& ctx);
    void apply(EngineContext& ctx);
    void applyUniforms();
    void rebuildWithDebugMode(int mode, EngineContext& ctx);
    // Auto-resolve depth map (g_Texture1) from the layer's .tex container (index 1)
    bool resolveDepth(const char* source_tex_path, EngineContext& ctx);

    void applyCompiledUniforms() {
        for (const auto& block : compiled.custom_uniform_blocks) {
            if (block.slot < 0 || block.uniform_names.empty()) continue;

            std::vector<float> packed(block.uniform_names.size() * 4, 0.0f);
            for (size_t i = 0; i < block.uniform_names.size(); ++i) {
                auto value = uniforms.find(block.uniform_names[i]);
                if (value == uniforms.end()) continue;
                for (size_t component = 0; component < value->second.size() && component < 4; ++component) {
                    packed[i * 4 + component] = value->second[component];
                }
            }

            sg_range range = {.ptr = packed.data(), .size = packed.size() * sizeof(float)};
            sg_apply_uniforms(block.slot, &range);
        }
    }

    render_effect_pass_t getRenderPass() {
        if (!geometry_classified) {
            is_fullscreen_quad = effectShaderUsesClipSpaceGeometry(stored_vs_source, shader_name.c_str());
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
        return r;
    }

    int debug_view_mode = 0;
    int debug_step = 0;  // 0=full shader, 1+ = forced texture output (bypasses main logic)

   private:
    std::string stored_vs_source;
    std::string stored_fs_source;
};

namespace wallpaper_engine {
struct EffectInstanceDocument;
}

class Effect {
   public:
    std::string file_path;
    std::vector<ShaderPass*> passes;
    bool visible = true;
    bool solo = false;

    Effect(cJSON* config, EngineContext& ctx);
    ~Effect();

    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;

    static Effect* load(const char* rel_path, cJSON* instance_config, EngineContext& ctx);
    static Effect* loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx);
    void init(EngineContext& ctx);
    void apply(EngineContext& ctx);
};

#endif  // EFFECT_H
