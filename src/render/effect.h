#ifndef EFFECT_H
#define EFFECT_H

#include <map>
#include <string>
#include <vector>

#include "../../libs/cJSON.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../core/gfx_resource.h"
#include "pass_textures.h"
#include "shader_compiler.h"

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

    int debug_view_mode = 0;
    int debug_step = 0;  // 0=full shader, 1-6=progressively simpler

   private:
    std::string stored_vs_source;
    std::string stored_fs_source;
};

class Effect {
   public:
    std::string file_path;
    std::vector<ShaderPass*> passes;
    bool visible = true;
    bool solo = false;

    Effect(cJSON* config, EngineContext& ctx);
    ~Effect();

    static Effect* load(const char* rel_path, cJSON* instance_config, EngineContext& ctx);
    void init(EngineContext& ctx);
    void apply(EngineContext& ctx);
};

#endif  // EFFECT_H
