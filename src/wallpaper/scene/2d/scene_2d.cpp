#include "scene_2d.h"

#include "render/render.h"
#include "sokol_app.h"
#include "wallpaper/scene/2d/layers/layer.h"
#include "wallpaper/scene/tree/scene_tree.h"

void Scene2DRuntime::init() {
    renderer_init(&ctx.renderer, (float)sapp_width(), (float)sapp_height());
}

void Scene2DRuntime::update(float dt) {
    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->update(dt, ctx);
        return;
    }
    for (auto layer : ctx.layers) layer->update(dt, ctx);
}

void Scene2DRuntime::draw() {
    const bool has_output_viewport = output_width > 0 && output_height > 0;
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, output_width, output_height, true);
        sg_apply_scissor_rect(output_x, output_y, output_width, output_height, true);
    }

    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->draw(ctx);
    } else {
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
            } else if (layer->visible) {
                layer->draw(ctx);
            }
        }
    }

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, sapp_width(), sapp_height(), true);
        sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
    }
}

void Scene2DRuntime::updateViewport() {
    float sw = output_width > 0 ? (float)output_width : (float)sapp_width();
    float sh = output_height > 0 ? (float)output_height : (float)sapp_height();
    renderer_update_viewport(&ctx.renderer, sw, sh);

    if (ctx.scene_w == 0 || ctx.scene_h == 0) return;

    float aspect_scene = ctx.scene_w / ctx.scene_h;
    float aspect_window = sw / sh;

    if (ctx.scaling_mode == SCALING_FIT) {
        ctx.render_scale = aspect_window > aspect_scene ? sh / ctx.scene_h : sw / ctx.scene_w;
    } else {
        ctx.render_scale = aspect_window > aspect_scene ? sw / ctx.scene_w : sh / ctx.scene_h;
    }

    ctx.offset_x = (sw - ctx.scene_w * ctx.render_scale) * 0.5f;
    ctx.offset_y = (sh - ctx.scene_h * ctx.render_scale) * 0.5f;
}

void Scene2DRuntime::setOutputViewport(int x, int y, int width, int height) {
    output_x = x;
    output_y = y;
    output_width = width;
    output_height = height;
}

void Scene2DRuntime::resetOutputViewport() {
    output_x = 0;
    output_y = 0;
    output_width = 0;
    output_height = 0;
}

void Scene2DRuntime::clearScene() {
    for (auto layer : ctx.layers) delete layer;
    ctx.layers.clear();
    delete ctx.scene_tree;
    ctx.scene_tree = nullptr;
    ctx.selected_object = -1;
    ctx.test_mode = false;
}

void Scene2DRuntime::cleanup() {
    clearScene();
    renderer_cleanup(&ctx.renderer);
}
