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

void scene_loader_init(void) {
    renderer_init(&state.renderer, (float)sapp_width(), (float)sapp_height());
    mkdir("extracted", 0755);
}

void scene_loader_load(const char* path) {
    strncpy(state.wallpaper_path, path, sizeof(state.wallpaper_path) - 1);
    strcpy(state.asset_root, "extracted");
    state.asset_mgr.init(state.engine_path, state.wallpaper_path);

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
    state.asset_mgr.init(state.engine_path, state.asset_root);

    char scene_path[1024];
    snprintf(scene_path, sizeof(scene_path), "%s/scene.json", state.asset_root);
    char* json_str = read_file_to_string(scene_path);
    if (!json_str) return;
    state.scene_json = cJSON_Parse(json_str);
    free(json_str);
    if (!state.scene_json) return;
    LOG_I("Scene JSON parsed successfully");

    // Detect Design Resolution
    state.scene_w = 0;
    state.scene_h = 0;

    // 1. Check root resolution
    cJSON* resolution = cJSON_GetObjectItemCaseSensitive(state.scene_json, "resolution");
    if (cJSON_IsString(resolution)) {
        sscanf(resolution->valuestring, "%f %f", &state.scene_w, &state.scene_h);
    }

    // 2. Check general -> orthogonalprojection (Common in 4K scenes)
    cJSON* general = cJSON_GetObjectItemCaseSensitive(state.scene_json, "general");
    if (state.scene_w == 0 && general) {
        cJSON* ortho = cJSON_GetObjectItemCaseSensitive(general, "orthogonalprojection");
        if (ortho) {
            cJSON* w = cJSON_GetObjectItemCaseSensitive(ortho, "width");
            cJSON* h = cJSON_GetObjectItemCaseSensitive(ortho, "height");
            if (cJSON_IsNumber(w)) state.scene_w = (float)w->valuedouble;
            if (cJSON_IsNumber(h)) state.scene_h = (float)h->valuedouble;
        }
    }

    // 3. Fallback to 1080p
    if (state.scene_w == 0) {
        state.scene_w = 1920.0f;
        state.scene_h = 1080.0f;
        LOG_W("Design resolution not found, defaulting to 1920x1080");
    } else {
        LOG_I("Detected Design Resolution: %.0fx%.0f", state.scene_w, state.scene_h);
    }

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
            Layer* layer = nullptr;
            layer = ParticleLayer::createFromJSON(obj_json);
            if (!layer) layer = ImageLayer::createFromJSON(obj_json);
            if (layer) state.layers.push_back(layer);
        }
    }

    scene_loader_update_viewport();
}

void scene_loader_update(float dt) {
    if (state.test_mode && state.selected_object >= 0 && state.selected_object < (int)state.layers.size()) {
        state.layers[state.selected_object]->update(dt);
        return;
    }
    for (auto layer : state.layers) layer->update(dt);
}

void scene_loader_draw(void) {
    if (state.test_mode && state.selected_object >= 0 && state.selected_object < (int)state.layers.size()) {
        state.layers[state.selected_object]->draw();
        return;
    }

    bool any_solo = false;
    for (auto layer : state.layers) {
        if (layer->solo) {
            any_solo = true;
            break;
        }
    }

    for (auto layer : state.layers) {
        if (any_solo) {
            if (layer->solo) layer->draw();
        } else {
            if (layer->visible) layer->draw();
        }
    }
}

void scene_loader_update_viewport(void) {
    float sw = (float)sapp_width();
    float sh = (float)sapp_height();
    renderer_update_viewport(&state.renderer, sw, sh);

    if (state.scene_w == 0 || state.scene_h == 0) return;

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
