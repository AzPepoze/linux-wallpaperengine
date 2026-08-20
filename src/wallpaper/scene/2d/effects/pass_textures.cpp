#include "pass_textures.h"

#include "core/logger.h"

namespace {
void ensure_slot(PassTextures& pass, int slot) {
    while ((int)pass.textures.size() <= slot) {
        pass.textures.push_back({});
        pass.texture_paths.push_back("");
        pass.texture_masks.push_back(true);
    }
}
}  // namespace

void PassTextures::loadFromConfig(cJSON* base_config, const std::string& shader_name, EngineContext& ctx) {
    cJSON* textures_node = cJSON_GetObjectItemCaseSensitive(base_config, "textures");
    if (!cJSON_IsArray(textures_node)) return;

    // Wallpaper Engine material/pass texture arrays are indexed by shader sampler:
    // textures[0] -> g_Texture0, textures[1] -> g_Texture1, ... . For image effects
    // g_Texture0 is the previous pass/current image and is supplied by the renderer,
    // so PassTextures owns only the extra slots beginning at g_Texture1.
    for (int source_slot = 1; source_slot < cJSON_GetArraySize(textures_node); ++source_slot) {
        cJSON* tex_node = cJSON_GetArrayItem(textures_node, source_slot);
        const int pass_idx = source_slot - 1;
        ensure_slot(*this, pass_idx);

        if (cJSON_IsString(tex_node) && tex_node->valuestring && tex_node->valuestring[0] != '\0') {
            std::string path;
            GfxImage img = ctx.asset_mgr.resolveTexture(tex_node->valuestring, &path);
            textures[pass_idx] = std::move(img);
            texture_paths[pass_idx] = path;
            effect_log.info("ShaderPass %s: g_Texture%d - Loaded base texture: %s", shader_name.c_str(), source_slot,
                            path.c_str());
        } else {
            effect_log.debug("ShaderPass %s: g_Texture%d - Empty/null entry", shader_name.c_str(), source_slot);
        }
    }
}

void PassTextures::applyInstanceOverrides(cJSON* instance_config, const std::string& shader_name, EngineContext& ctx) {
    if (!instance_config) return;

    cJSON* inst_textures = cJSON_GetObjectItemCaseSensitive(instance_config, "textures");
    if (!cJSON_IsArray(inst_textures)) return;

    // Instance pass arrays use the same shader-slot indexing. Slot 0 is the input
    // render target and cannot be overridden as an extra texture here.
    for (int source_slot = 1; source_slot < cJSON_GetArraySize(inst_textures); ++source_slot) {
        cJSON* tex_node = cJSON_GetArrayItem(inst_textures, source_slot);
        if (!cJSON_IsString(tex_node) || !tex_node->valuestring || tex_node->valuestring[0] == '\0') continue;

        const int pass_idx = source_slot - 1;
        std::string path;
        GfxImage img = ctx.asset_mgr.resolveTexture(tex_node->valuestring, &path);
        if (img.id == SG_INVALID_ID) continue;

        ensure_slot(*this, pass_idx);
        const bool replacing = textures[pass_idx].id != SG_INVALID_ID;
        textures[pass_idx] = std::move(img);
        texture_paths[pass_idx] = path;
        effect_log.info("ShaderPass %s: g_Texture%d - Override (%s): %s", shader_name.c_str(), source_slot,
                        replacing ? "Replace" : "Add", path.c_str());
    }
}

bool PassTextures::resolveDepth(const char* source_tex_path, const std::string& shader_name, EngineContext& ctx) {
    if (depth_attempted || !source_tex_path) return false;
    depth_attempted = true;

    if (textures.empty() || textures[0].id == SG_INVALID_ID) {
        std::string depth_path;
        GfxImage depth_img = ctx.asset_mgr.resolveTexture(source_tex_path, &depth_path, 1);
        if (depth_img.id != SG_INVALID_ID) {
            ensure_slot(*this, 0);
            textures[0] = std::move(depth_img);
            texture_paths[0] = depth_path + "#1";
            texture_masks[0] = true;
            buildCachedViews();
            effect_log.info("ShaderPass %s: Auto-resolved depth map (g_Texture1) from %s", shader_name.c_str(),
                            depth_path.c_str());
            return true;
        }
    }
    return false;
}

void PassTextures::buildCachedViews() {
    cached_views.clear();
    cached_views.reserve(textures.size());
    for (auto& img : textures) {
        if (img.id != SG_INVALID_ID) {
            sg_view_desc v_desc = {};
            v_desc.texture.image = img;
            cached_views.push_back(sg_make_view(&v_desc));
        } else {
            cached_views.push_back({});
        }
    }
}
