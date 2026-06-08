#ifndef EFFECT_H
#define EFFECT_H

#include <string>
#include <vector>

#include "../../libs/cJSON.h"
#include "../../libs/sokol/sokol_gfx.h"

class ShaderPass {
   public:
    std::string shader_name;
    sg_pipeline pipeline = {SG_INVALID_ID};
    std::vector<sg_image> textures;
    cJSON* constant_values;
    bool enabled = true;

    ShaderPass(cJSON* config);
    ~ShaderPass();

    void apply();
    void showInspector(int id);
};

class Effect {
   public:
    std::string file_path;
    std::vector<ShaderPass*> passes;
    bool visible = true;

    Effect(cJSON* config);
    ~Effect();

    static Effect* load(const char* rel_path, cJSON* instance_config);
    void apply();
    void showInspector(int id);
};

#endif  // EFFECT_H
