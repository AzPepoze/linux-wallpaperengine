#include "pass_textures.h"

#include "../core/logger.h"

void PassTextures::loadFromConfig(cJSON* base_config, const std::string& shader_name, EngineContext& ctx) {
    cJSON* textures_node = cJSON_GetObjectItemCaseSensitive(base_config, "textures");
    if (cJSON_IsArray(textures_node)) {
        cJSON* tex_node;
        cJSON_ArrayForEach(tex_node, textures_node) {
            int current_slot = (int)textures.size();
            if (cJSON_IsString(tex_node)) {
                std::string path;
                GfxImage img = ctx.asset_mgr.resolveTexture(tex_node->valuestring, &path);
                textures.push_back(std::move(img));
                texture_paths.push_back(path);
                texture_masks.push_back(true);
                effect_log.info("ShaderPass %s: Slot %d (g_Texture%d) - Loaded base texture: %s", shader_name.c_str(),
                                current_slot, current_slot + 1, path.c_str());
            } else if (cJSON_IsNull(tex_node)) {
                textures.push_back({});
                texture_paths.push_back("");
                texture_masks.push_back(true);
                effect_log.debug("ShaderPass %s: Slot %d (g_Texture%d) - Null entry (auto-resolve pending)",
                                 shader_name.c_str(), current_slot, current_slot + 1);
            }
        }
    }
}

void PassTextures::applyInstanceOverrides(cJSON* instance_config, const std::string& shader_name, EngineContext& ctx) {
    if (!instance_config) return;

    cJSON* inst_textures = cJSON_GetObjectItemCaseSensitive(instance_config, "textures");
    if (cJSON_IsArray(inst_textures)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_textures); i++) {
            cJSON* tex_node = cJSON_GetArrayItem(inst_textures, i);
            if (i == 0) continue;  // Slot 0 is main view

            int pass_idx = i - 1;
            if (cJSON_IsString(tex_node)) {
                std::string path;
                GfxImage img = ctx.asset_mgr.resolveTexture(tex_node->valuestring, &path);
                if (img.id != SG_INVALID_ID) {
                    if (pass_idx < (int)textures.size()) {
                        textures[pass_idx] = std::move(img);
                        texture_paths[pass_idx] = path;
                        effect_log.info("ShaderPass %s: Slot %d (g_Texture%d) - Override (Replace): %s",
                                        shader_name.c_str(), pass_idx, i, path.c_str());
                    } else {
                        while ((int)textures.size() < pass_idx) {
                            textures.push_back({});
                            texture_paths.push_back("");
                            texture_masks.push_back(true);
                        }
                        textures.push_back(std::move(img));
                        texture_paths.push_back(path);
                        texture_masks.push_back(true);
                        effect_log.info("ShaderPass %s: Slot %d (g_Texture%d) - Override (Add): %s",
                                        shader_name.c_str(), pass_idx, i, path.c_str());
                    }
                }
            }
        }
    }
}

void PassTextures::buildCachedViews() {
    cached_views.clear();
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
