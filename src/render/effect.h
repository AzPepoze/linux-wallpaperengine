#ifndef EFFECT_H
#define EFFECT_H

#include <map>
#include <string>
#include <vector>

#include "../../libs/cJSON.h"
#include "../../libs/sokol/sokol_gfx.h"

class ShaderPass {
   public:
    std::string shader_name;
    sg_pipeline pipeline = {SG_INVALID_ID};
    sg_shader shader = {SG_INVALID_ID};
    std::vector<sg_image> textures;
    std::vector<sg_view> cached_views;
    std::vector<std::string> texture_paths;
    std::vector<bool> texture_masks;
    cJSON* constant_values;
    std::map<std::string, std::vector<float>> uniforms;
    std::map<int, std::string> texture_labels;
    bool enabled = true;
    bool show_files = false;

    ShaderPass(cJSON* config, cJSON* instance_config = nullptr);
    ~ShaderPass();

    void init();
    void apply();
    void applyUniforms();
    void showInspector(int id);
    void rebuildWithDebugMode(int mode);

    // Auto-resolve depth map (g_Texture1) from the layer's .tex container (index 1)
    bool resolveDepth(const char* source_tex_path);

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

    Effect(cJSON* config);
    ~Effect();

    static Effect* load(const char* rel_path, cJSON* instance_config);
    void init();
    void apply();
    void showInspector(int id);
};

#endif  // EFFECT_H
