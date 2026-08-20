#include "scene_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libs/sokol/sokol_app.h"
#include "../core/engine_context.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "../render/render.h"
#include "layer.h"

void SceneRenderer::init() {
    renderer_init(&ctx.renderer, (float)sapp_width(), (float)sapp_height());
}

void SceneRenderer::update(float dt) {
    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->update(dt, ctx);
        return;
    }
    for (auto layer : ctx.layers) layer->update(dt, ctx);
}

void SceneRenderer::draw() {
    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->draw(ctx);
        return;
    }

    bool any_solo = false;
    for (auto layer : ctx.layers) {
        if (layer->solo) {
            any_solo = true;
            break;
        }
    }

    for (auto layer : ctx.layers) {
        if (any_solo) {
            if (layer->solo) layer->draw(ctx);
        } else {
            if (layer->visible) layer->draw(ctx);
        }
    }
}

void SceneRenderer::updateViewport() {
    float sw = (float)sapp_width();
    float sh = (float)sapp_height();
    renderer_update_viewport(&ctx.renderer, sw, sh);

    if (ctx.scene_w == 0 || ctx.scene_h == 0) return;

    float aspect_scene = ctx.scene_w / ctx.scene_h;
    float aspect_window = sw / sh;

    if (ctx.scaling_mode == SCALING_FIT) {
        if (aspect_window > aspect_scene) {
            ctx.render_scale = sh / ctx.scene_h;
        } else {
            ctx.render_scale = sw / ctx.scene_w;
        }
    } else {  // COVER
        if (aspect_window > aspect_scene) {
            ctx.render_scale = sw / ctx.scene_w;
        } else {
            ctx.render_scale = sh / ctx.scene_h;
        }
    }

    ctx.offset_x = (sw - ctx.scene_w * ctx.render_scale) * 0.5f;
    ctx.offset_y = (sh - ctx.scene_h * ctx.render_scale) * 0.5f;
}

void SceneRenderer::cleanup() {
    for (auto l : ctx.layers) delete l;
    ctx.layers.clear();
    if (ctx.scene_json) cJSON_Delete(ctx.scene_json);
    ctx.scene_json = nullptr;
    renderer_cleanup(&ctx.renderer);
}
