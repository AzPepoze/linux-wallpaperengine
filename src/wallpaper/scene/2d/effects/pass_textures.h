#ifndef PASS_TEXTURES_H
#define PASS_TEXTURES_H

#include <cjson/cJSON.h>

#include <map>
#include <string>
#include <vector>

#include "core/engine_context.h"
#include "core/gfx_resource.h"
#include "sokol_gfx.h"

class PassTextures {
   public:
    GfxImage texture0;
    GfxView texture0_view;
    std::string texture0_path;
    std::vector<GfxImage> textures;
    std::vector<GfxView> cached_views;
    std::vector<std::string> texture_paths;
    std::vector<bool> texture_masks;
    bool depth_attempted = false;

    void loadFromConfig(cJSON* base_config, const std::string& shader_name, EngineContext& ctx);
    void applyInstanceOverrides(cJSON* instance_config, const std::string& shader_name, EngineContext& ctx);

    bool resolveDepth(const char* source_tex_path, const std::string& shader_name, EngineContext& ctx);

    void buildCachedViews();
};

#endif  // PASS_TEXTURES_H
