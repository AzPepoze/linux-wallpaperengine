#include "scene_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../libs/sokol/sokol_app.h"
#include "../core/context.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "../scene/2d/image_layer.h"
#include "../scene/2d/particle_layer.h"
#include "texture.h"
#include "unpack.h"

static sg_image resolve_texture(const char* name, std::string* out_path = nullptr) {
    char abs_path[1024];
    char name_with_ext[256];
    if (!strstr(name, "."))
        snprintf(name_with_ext, sizeof(name_with_ext), "%s.tex", name);
    else
        strncpy(name_with_ext, name, sizeof(name_with_ext) - 1);

    if (asset_resolve_path(&state.asset_mgr, name_with_ext, abs_path, sizeof(abs_path))) {
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

static sg_image resolve_material_texture(const char* mat_rel_path, std::string* out_path = nullptr) {
    char abs_path[1024];
    if (!asset_resolve_path(&state.asset_mgr, mat_rel_path, abs_path, sizeof(abs_path)))
        return (sg_image){SG_INVALID_ID};

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
                img = resolve_texture(rel_tex, out_path);
                if (img.id == SG_INVALID_ID) img = resolve_texture(tex_node->valuestring, out_path);
            }
        }
    }
    cJSON_Delete(mat_json);
    return img;
}

static void load_material(const char* mat_rel_path, ImageLayer* layer) {
    layer->img = resolve_material_texture(mat_rel_path, &layer->path);
}

static void load_model(const char* mdl_rel_path, ImageLayer* layer) {
    char abs_path[1024];
    if (!asset_resolve_path(&state.asset_mgr, mdl_rel_path, abs_path, sizeof(abs_path))) return;
    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return;
    cJSON* mdl_json = cJSON_Parse(json_str);
    free(json_str);
    if (!mdl_json) return;
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(mdl_json, "material");
    if (cJSON_IsString(mat_ref)) load_material(mat_ref->valuestring, layer);
    cJSON_Delete(mdl_json);
}

void scene_loader_init(void) {
    renderer_init(&state.renderer, (float)sapp_width(), (float)sapp_height());
    mkdir("extracted", 0755);
    mkdir("converted", 0755);
}

void scene_loader_load(const char* path) {
    strncpy(state.wallpaper_path, path, sizeof(state.wallpaper_path) - 1);
    strcpy(state.asset_root, "extracted");
    asset_manager_init(&state.asset_mgr, state.engine_path, state.wallpaper_path);
    if (state.is_pkg)
        extract_pkg(state.wallpaper_path, "extracted");
    else {
        char pkg_file[1024];
        snprintf(pkg_file, sizeof(pkg_file), "%s/scene.pkg", state.wallpaper_path);
        if (access(pkg_file, F_OK) == 0)
            extract_pkg(pkg_file, "extracted");
        else
            strncpy(state.asset_root, state.wallpaper_path, sizeof(state.asset_root) - 1);
    }
    asset_manager_init(&state.asset_mgr, state.engine_path, state.asset_root);
    char scene_path[1024];
    snprintf(scene_path, sizeof(scene_path), "%s/scene.json", state.asset_root);
    char* json_str = read_file_to_string(scene_path);
    if (!json_str) return;
    state.scene_json = cJSON_Parse(json_str);
    free(json_str);
    if (!state.scene_json) return;
    LOG_I("Scene JSON parsed successfully");
    state.scene_w = 1920.0f;
    state.scene_h = 1080.0f;
    cJSON* resolution = cJSON_GetObjectItemCaseSensitive(state.scene_json, "resolution");
    if (cJSON_IsString(resolution)) sscanf(resolution->valuestring, "%f %f", &state.scene_w, &state.scene_h);

    cJSON* general = cJSON_GetObjectItemCaseSensitive(state.scene_json, "general");
    if (general) {
        cJSON* cc = cJSON_GetObjectItemCaseSensitive(general, "clearcolor");
        if (cJSON_IsString(cc)) {
            float r, g, b;
            if (sscanf(cc->valuestring, "%f %f %f", &r, &g, &b) == 3) {
                state.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
                state.pass_action.colors[0].clear_value = {r, g, b, 1.0f};
            }
        }
    }

    cJSON* objects = cJSON_GetObjectItemCaseSensitive(state.scene_json, "objects");
    if (cJSON_IsArray(objects)) {
        for (auto l : state.layers) delete l;
        state.layers.clear();

        cJSON* obj_json;
        cJSON_ArrayForEach(obj_json, objects) {
            cJSON* name_node = cJSON_GetObjectItemCaseSensitive(obj_json, "name");
            const char* name = name_node ? name_node->valuestring : "Layer";

            Layer* layer = nullptr;
            float norm = 1.0f;

            cJSON* type_node = cJSON_GetObjectItemCaseSensitive(obj_json, "type");
            const char* type = type_node ? type_node->valuestring : "image";

            if (strcmp(type, "particle") == 0) {
                cJSON* p_file = cJSON_GetObjectItemCaseSensitive(obj_json, "file");
                if (cJSON_IsString(p_file)) {
                    char p_abs[1024];
                    if (asset_resolve_path(&state.asset_mgr, p_file->valuestring, p_abs, sizeof(p_abs))) {
                        char* p_json_str = read_file_to_string(p_abs);
                        if (p_json_str) {
                            cJSON* p_json = cJSON_Parse(p_json_str);
                            free(p_json_str);
                            if (p_json) {
                                std::string p_tex_path;
                                sg_image p_tex = resolve_texture("materials/particle.tex", &p_tex_path);
                                cJSON* mat = cJSON_GetObjectItemCaseSensitive(p_json, "material");
                                if (cJSON_IsString(mat)) {
                                    sg_image mat_tex = resolve_material_texture(mat->valuestring, &p_tex_path);
                                    if (mat_tex.id != SG_INVALID_ID) p_tex = mat_tex;
                                }
                                ParticleSystem* ps = new ParticleSystem(p_json, p_tex, state.scene_w, state.scene_h);
                                ps->config_path = p_abs;
                                ps->texture_path = p_tex_path;
                                layer = new ParticleLayer(name, ps);
                                layer->path = p_abs;
                            }
                        }
                    }
                }
            } else {
                ImageLayer* img_layer = new ImageLayer(name, (sg_image){SG_INVALID_ID});
                cJSON* asset_path = cJSON_GetObjectItemCaseSensitive(obj_json, "image");
                if (!cJSON_IsString(asset_path)) asset_path = cJSON_GetObjectItemCaseSensitive(obj_json, "model");

                if (cJSON_IsString(asset_path)) {
                    if (strstr(asset_path->valuestring, ".json"))
                        load_model(asset_path->valuestring, img_layer);
                    else
                        img_layer->img = resolve_texture(asset_path->valuestring, &img_layer->path);

                    if (img_layer->img.id != SG_INVALID_ID) {
                        sg_image_desc desc = sg_query_image_desc(img_layer->img);
                        float asset_w = (float)desc.width;
                        float asset_h = (float)desc.height;
                        float norm_x = state.scene_w / asset_w;
                        float norm_y = state.scene_h / asset_h;
                        norm = (norm_x > norm_y) ? norm_x : norm_y;
                        img_layer->size[0] = asset_w * norm;
                        img_layer->size[1] = asset_h * norm;
                    }
                }
                layer = img_layer;
            }

            if (layer) {
                cJSON* origin = cJSON_GetObjectItemCaseSensitive(obj_json, "origin");
                if (cJSON_IsString(origin)) {
                    float ox, oy, oz;
                    if (sscanf(origin->valuestring, "%f %f %f", &ox, &oy, &oz) >= 2) {
                        layer->origin[0] = ox * norm;
                        layer->origin[1] = oy * norm;
                        layer->origin[2] = oz * norm;
                    }
                }
                cJSON* parallax = cJSON_GetObjectItemCaseSensitive(obj_json, "parallax");
                if (cJSON_IsString(parallax)) {
                    sscanf(parallax->valuestring, "%f %f", &layer->parallax[0], &layer->parallax[1]);
                }
                state.layers.push_back(layer);
            }
        }
    }
}

void scene_loader_update(float dt) {
    for (auto layer : state.layers) layer->update(dt);
}

void scene_loader_draw(void) {
    for (auto layer : state.layers) {
        LOG_TAG_D("SCENE", "Drawing layer: %s", layer->name.c_str());
        layer->draw();
    }
}

void scene_loader_update_viewport(void) {
    float sw = (float)sapp_width();
    float sh = (float)sapp_height();
    renderer_update_viewport(&state.renderer, sw, sh);

    float aspect_scene = state.scene_w / state.scene_h;
    float aspect_window = sw / sh;

    if (state.scaling_mode == SCALING_FIT) {
        if (aspect_window > aspect_scene) {
            state.render_scale = sh / state.scene_h;
        } else {
            state.render_scale = sw / state.scene_w;
        }
    } else {  // COVER
        if (aspect_window > aspect_scene) {
            state.render_scale = sw / state.scene_w;
        } else {
            state.render_scale = sh / state.scene_h;
        }
    }

    state.offset_x = (sw - state.scene_w * state.render_scale) * 0.5f;
    state.offset_y = (sh - state.scene_h * state.render_scale) * 0.5f;
}

void scene_loader_cleanup(void) {
    for (auto l : state.layers) delete l;
    state.layers.clear();
    if (state.scene_json) cJSON_Delete(state.scene_json);
    renderer_cleanup(&state.renderer);
}
