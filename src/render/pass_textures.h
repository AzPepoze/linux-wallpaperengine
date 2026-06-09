#ifndef PASS_TEXTURES_H
#define PASS_TEXTURES_H

#include <map>
#include <string>
#include <vector>

#include "../../libs/cJSON.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../core/engine_context.h"
#include "../core/gfx_resource.h"

class PassTextures {
   public:
    std::vector<GfxImage> textures;
    std::vector<GfxView> cached_views;
    std::vector<std::string> texture_paths;
    std::vector<bool> texture_masks;

    void loadFromConfig(cJSON* base_config, const std::string& shader_name, EngineContext& ctx);
    void applyInstanceOverrides(cJSON* instance_config, const std::string& shader_name, EngineContext& ctx);
    void buildCachedViews();
};

#endif  // PASS_TEXTURES_H
