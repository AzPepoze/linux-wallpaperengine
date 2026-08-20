#ifndef EFFECT_H
#define EFFECT_H

#include <map>
#include <string>
#include <vector>

#include "cJSON.h"
#include "core/gfx_resource.h"
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
    cJSON* constant_values;
    std::map<std::string, std::vector<float>> uniforms;
    std::map<int, std::string> texture_labels;
    bool enabled = true;
    bool show_files = false;

    ShaderPass(cJSON* config, cJSON* instance_config, EngineContext& ctx);
    ~ShaderPass();

    void init(EngineContext& ctx);
    void apply(EngineContext& ctx);
    void applyUniforms();
    void rebuildWithDebugMode(int mode, EngineContext& ctx);
    // Auto-resolve depth map (g_Texture1) from the layer's .tex container (index 1)
    bool resolveDepth(const char* source_tex_path, EngineContext& ctx);

    render_effect_pass_t getRenderPass() const {
        render_effect_pass_t r = {};
        r.enabled = enabled;
        r.pipeline = compiled.pipeline;
        r.shader_name = shader_name.c_str();
        r.extra_views = pass_textures.cached_views.data();
        r.num_extra_views = pass_textures.cached_views.size();
        r.apply_custom_uniforms = [](void* ud) { static_cast<ShaderPass*>(ud)->applyUniforms(); };
        r.user_data = const_cast<ShaderPass*>(this);
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

    static Effect* load(const char* rel_path, cJSON* instance_config, EngineContext& ctx);
    static Effect* loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx);
    void init(EngineContext& ctx);
    void apply(EngineContext& ctx);
};

#endif  // EFFECT_H
