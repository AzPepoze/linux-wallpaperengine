#include "asset_manager.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../libs/cJSON.h"
#include "../core/utils.h"
#include "texture.h"

void AssetManager::init(const char* ep, const char* wp) {
    engine_path = ep;
    wallpaper_path = wp;
}

bool AssetManager::resolvePath(const char* rel_path, char* out_abs_path, int max_len) const {
    // 1. Try Local Wallpaper Path (or tmp extracted)
    snprintf(out_abs_path, max_len, "%s/%s", wallpaper_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 2. Try Engine Assets Path
    snprintf(out_abs_path, max_len, "%s/assets/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 3. Try specifically under assets/materials/ (common for particles and masks)
    snprintf(out_abs_path, max_len, "%s/assets/materials/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 4. Try under materials/ in the wallpaper/extracted root (common for masks)
    snprintf(out_abs_path, max_len, "%s/materials/%s", wallpaper_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    // 5. Try specifically under assets/presets/
    const char* filename = strrchr(rel_path, '/');
    if (filename) {
        filename++;
        if (strstr(rel_path, "materials/presets/") == rel_path) {
            char preset_name[64];
            const char* start = rel_path + 18;
            const char* end = strstr(start, ".json");
            if (end) {
                size_t len = end - start;
                if (len > 4) len -= 4;
                strncpy(preset_name, start, len);
                preset_name[len] = '\0';
                snprintf(out_abs_path, max_len, "%s/assets/presets/%s/%s", engine_path.c_str(), preset_name, rel_path);
                if (access(out_abs_path, F_OK) == 0) return true;
            }
        }
    }

    // 5. Fallback
    snprintf(out_abs_path, max_len, "%s/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    return false;
}

sg_image AssetManager::resolveTexture(const char* name, std::string* out_path) const {
    char abs_path[1024];
    char name_with_ext[256];
    if (!strstr(name, "."))
        snprintf(name_with_ext, sizeof(name_with_ext), "%s.tex", name);
    else
        strncpy(name_with_ext, name, sizeof(name_with_ext) - 1);

    if (resolvePath(name_with_ext, abs_path, sizeof(abs_path))) {
        if (out_path) *out_path = abs_path;
        DecodedTexture tex = load_texture(abs_path);
        if (tex.pixels) {
            sg_image_desc desc = {};
            desc.width = (int)tex.width;
            desc.height = (int)tex.height;
            desc.pixel_format = tex.format;
            desc.data.mip_levels[0] = {tex.pixels, tex.data_size};
            sg_image img = sg_make_image(&desc);
            free_texture(tex);
            return img;
        }
    }
    return (sg_image){SG_INVALID_ID};
}

sg_image AssetManager::resolveMaterialTexture(const char* mat_rel_path, std::string* out_path) const {
    char abs_path[1024];
    if (!resolvePath(mat_rel_path, abs_path, sizeof(abs_path))) return (sg_image){SG_INVALID_ID};

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return (sg_image){SG_INVALID_ID};

    cJSON* mat_json = cJSON_Parse(json_str);
    free(json_str);
    if (!mat_json) return (sg_image){SG_INVALID_ID};

    sg_image img = {SG_INVALID_ID};

    char mat_dir[512] = "";
    const char* last_slash = strrchr(mat_rel_path, '/');
    if (last_slash) {
        size_t len = last_slash - mat_rel_path;
        strncpy(mat_dir, mat_rel_path, len);
        mat_dir[len] = '/';
        mat_dir[len + 1] = '\0';
    }

    cJSON* passes = cJSON_GetObjectItemCaseSensitive(mat_json, "passes");
    if (cJSON_IsArray(passes)) {
        cJSON* pass = cJSON_GetArrayItem(passes, 0);
        cJSON* textures = cJSON_GetObjectItemCaseSensitive(pass, "textures");
        if (cJSON_IsArray(textures)) {
            cJSON* tex_node = cJSON_GetArrayItem(textures, 0);
            if (cJSON_IsString(tex_node)) {
                char rel_tex[512];
                snprintf(rel_tex, sizeof(rel_tex), "%s%s", mat_dir, tex_node->valuestring);
                img = resolveTexture(rel_tex, out_path);
                if (img.id == SG_INVALID_ID) img = resolveTexture(tex_node->valuestring, out_path);
            }
        }
    }
    cJSON_Delete(mat_json);
    return img;
}
