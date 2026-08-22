#include "pass_textures.h"

#include "shared/core/logger.h"

namespace {
void ensure_slot(PassTextures& pass, int slot) {
    while ((int)pass.textures.size() <= slot) {
        pass.textures.push_back({});
        pass.texture_paths.push_back("");
        pass.texture_masks.push_back(true);
    }
}

void load_texture0(PassTextures& pass, cJSON* tex_node, const std::string& shader_name, EngineContext& ctx,
                   bool allow_clear) {
    if (cJSON_IsString(tex_node) && tex_node->valuestring) {
        if (tex_node->valuestring[0] == '\0') {
            if (allow_clear) {
                pass.texture0 = {};
                pass.texture0_view = {};
                pass.texture0_path.clear();
                effect_log.debug("ShaderPass %s: g_Texture0 - Using current effect input", shader_name.c_str());
            }
            return;
        }

        std::string path;
        GfxImage img = ctx.asset_mgr.resolveTexture(tex_node->valuestring, &path);
        if (img.id == SG_INVALID_ID) {
            effect_log.warn("ShaderPass %s: g_Texture0 - Failed to load explicit texture: %s", shader_name.c_str(),
                            tex_node->valuestring);
            return;
        }

        pass.texture0 = std::move(img);
        pass.texture0_path = path;
        effect_log.info("ShaderPass %s: g_Texture0 - Loaded explicit texture: %s", shader_name.c_str(), path.c_str());
    }
}
}  // namespace

void PassTextures::loadFromConfig(cJSON* base_config, const std::string& shader_name, EngineContext& ctx) {
    cJSON* textures_node = cJSON_GetObjectItemCaseSensitive(base_config, "textures");
    if (!cJSON_IsArray(textures_node)) return;

    // Wallpaper Engine binds material texture array indices directly to g_TextureN.
    // For image effects only an empty g_Texture0 means "use the current/previous pass input".
    // Preserve an explicitly authored textures[0] instead of unconditionally replacing it.
    if (cJSON_GetArraySize(textures_node) > 0) {
        load_texture0(*this, cJSON_GetArrayItem(textures_node, 0), shader_name, ctx, true);
    }

    // Extra pass-owned textures begin at g_Texture1 and are exposed to the renderer
    // through cached_views[0]. Keep empty/null logical slots so sampler indices stay aligned.
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

    if (cJSON_GetArraySize(inst_textures) > 0) {
        load_texture0(*this, cJSON_GetArrayItem(inst_textures, 0), shader_name, ctx, true);
    }

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
    texture0_view = {};
    if (texture0.id != SG_INVALID_ID) {
        sg_view_desc v_desc = {};
        v_desc.texture.image = texture0;
        texture0_view = sg_make_view(&v_desc);
    }

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
