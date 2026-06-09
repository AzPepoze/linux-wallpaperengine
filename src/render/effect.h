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
