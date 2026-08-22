#include "scene_2d.h"

#include "render/render.h"
#include "sokol_app.h"
#include "wallpaper/scene/2d/layers/image_layer.h"
#include "wallpaper/scene/2d/layers/layer.h"
#include "wallpaper/scene/2d/layers/particle_layer.h"
#include "wallpaper/scene/tree/scene_tree.h"

#if DEBUG_BUILD
#include "render/diagnostics/render_diagnostics.h"
#endif

void Scene2DRuntime::init() {
    renderer_init(&ctx.renderer, (float)sapp_width(), (float)sapp_height());
    renderer_precompile_blend_pipelines(ctx, &ctx.renderer);
}

void Scene2DRuntime::update(float dt) {
    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->update(dt, ctx);
        return;
    }
    for (auto layer : ctx.layers) layer->update(dt, ctx);
}

bool Scene2DRuntime::requiresOffscreenComposition() const {
    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        const auto* particle = dynamic_cast<const ParticleLayer*>(ctx.layers[ctx.selected_object]);
        return particle && particle->requiresSceneColor();
    }

    bool any_solo = false;
    for (const auto* layer : ctx.layers) {
        if (layer->solo) {
            any_solo = true;
            break;
        }
    }

    for (const auto* layer : ctx.layers) {
        if ((any_solo && !layer->solo) || (!any_solo && !layer->visible)) continue;
        const auto* particle = dynamic_cast<const ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) return true;
        const auto* image = dynamic_cast<const ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) return true;
    }
    return false;
}

bool Scene2DRuntime::ensureSceneTargets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (scene_targets[0].image.id != SG_INVALID_ID && scene_targets[0].width == width &&
        scene_targets[0].height == height && scene_targets[1].image.id != SG_INVALID_ID &&
        scene_targets[1].width == width && scene_targets[1].height == height) {
        return true;
    }

    scene_targets[0] = SceneTarget{};
    scene_targets[1] = SceneTarget{};

    for (SceneTarget& target : scene_targets) {
        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        target.image = sg_make_image(&image_desc);
        if (target.image.id == SG_INVALID_ID) return false;

        sg_view_desc texture_desc = {};
        texture_desc.texture.image = target.image;
        target.texture_view = sg_make_view(&texture_desc);

        sg_view_desc attachment_desc = {};
        attachment_desc.color_attachment.image = target.image;
        target.attachment_view = sg_make_view(&attachment_desc);
        target.width = width;
        target.height = height;

        if (target.texture_view.id == SG_INVALID_ID || target.attachment_view.id == SG_INVALID_ID) return false;
    }
    return true;
}

void Scene2DRuntime::drawDirect() {
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

void Scene2DRuntime::drawOffscreen() {
    const int width = output_width > 0 ? output_width : sapp_width();
    const int height = output_height > 0 ? output_height : sapp_height();
    if (!ensureSceneTargets(width, height)) {
        scene_output_index = -1;
        return;
    }

    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_scissor_rect(0, 0, width, height, true);

    int current = 0;
    sg_pass clear_pass = {};
    clear_pass.action = ctx.pass_action;
    clear_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    clear_pass.attachments.colors[0] = scene_targets[current].attachment_view;
    sg_begin_pass(&clear_pass);
    sg_end_pass();

    int layer_index = 0;
    auto capture_layer_result = [&](Layer* layer) {
#if DEBUG_BUILD
        RenderDiagnostics& diagnostics = RenderDiagnostics::instance();
        if (!diagnostics.is_capturing_frame) return;

        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        sg_image snapshot = sg_make_image(&image_desc);
        if (snapshot.id == SG_INVALID_ID) return;
        sg_view_desc source_view_desc = {};
        source_view_desc.texture.image = snapshot;
        sg_view snapshot_texture = sg_make_view(&source_view_desc);
        sg_view_desc attachment_view_desc = {};
        attachment_view_desc.color_attachment.image = snapshot;
        sg_view snapshot_attachment = sg_make_view(&attachment_view_desc);
        if (snapshot_texture.id == SG_INVALID_ID || snapshot_attachment.id == SG_INVALID_ID) {
            if (snapshot_texture.id != SG_INVALID_ID) sg_destroy_view(snapshot_texture);
            if (snapshot_attachment.id != SG_INVALID_ID) sg_destroy_view(snapshot_attachment);
            sg_destroy_image(snapshot);
            return;
        }

        sg_pass snapshot_pass = {};
        snapshot_pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        snapshot_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        snapshot_pass.attachments.colors[0] = snapshot_attachment;
        sg_begin_pass(&snapshot_pass);
        float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                             0.0f, 0.0f, (float)width, (float)height, 0.0f, white, false, nullptr);
        sg_end_pass();

        char stage_name[320];
        if (const auto* image = dynamic_cast<const ImageLayer*>(layer)) {
            snprintf(stage_name, sizeof(stage_name), "after-%02d-id-%u-%s-alpha-%.3f-blend-%d", layer_index++,
                     image->scene_object_id, layer->name.c_str(), image->tint[3], image->color_blend_mode);
        } else {
            snprintf(stage_name, sizeof(stage_name), "after-%02d-id-%u-%s", layer_index++, layer->scene_object_id,
                     layer->name.c_str());
        }
        diagnostics.recordSceneStage(stage_name, snapshot, snapshot_texture, snapshot_attachment);
#else
        (void)layer;
#endif
    };

    auto draw_layer = [&](Layer* layer) {
        auto* particle = dynamic_cast<ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) {
            const int next = 1 - current;
            sg_pass composite_pass = {};
            composite_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            composite_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            composite_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
            composite_pass.attachments.colors[0] = scene_targets[next].attachment_view;
            sg_begin_pass(&composite_pass);

            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0.0f, 0.0f, (float)width, (float)height, 0.0f, white, false, nullptr);
            particle->setSceneColorView(scene_targets[current].texture_view);
            particle->draw(ctx);
            sg_end_pass();
            current = next;
            capture_layer_result(layer);
            return;
        }

        auto* image = dynamic_cast<ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) {
            const int next = 1 - current;
            sg_pass composite_pass = {};
            composite_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            composite_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            composite_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
            composite_pass.attachments.colors[0] = scene_targets[next].attachment_view;
            sg_begin_pass(&composite_pass);
            float white[4] = {1, 1, 1, 1};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0, 0, (float)width, (float)height, 0, white, false, nullptr);
            image->drawComposite(ctx, scene_targets[current].texture_view);
            sg_end_pass();
            current = next;
            capture_layer_result(layer);
            return;
        }

        sg_pass layer_pass = {};
        layer_pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        layer_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        layer_pass.attachments.colors[0] = scene_targets[current].attachment_view;
        sg_begin_pass(&layer_pass);
        layer->draw(ctx);
        sg_end_pass();
        capture_layer_result(layer);
    };

    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        draw_layer(ctx.layers[ctx.selected_object]);
    } else {
        bool any_solo = false;
        for (auto layer : ctx.layers) {
            if (layer->solo) {
                any_solo = true;
                break;
            }
        }
        for (auto layer : ctx.layers) {
            if ((any_solo && !layer->solo) || (!any_solo && !layer->visible)) continue;
            draw_layer(layer);
        }
    }

    scene_output_index = current;
}

void Scene2DRuntime::draw() {
    if (requiresOffscreenComposition())
        drawOffscreen();
    else {
        scene_output_index = -1;
        drawDirect();
    }
}

void Scene2DRuntime::drawParticleDiagnostics() {
    if (!ctx.particle_debug_bounds && !ctx.particle_debug_velocity) return;
    for (Layer* layer : ctx.layers) {
        if (!layer->visible) continue;
        if (auto* particle = dynamic_cast<ParticleLayer*>(layer)) particle->drawDebug(ctx);
    }
}

void Scene2DRuntime::present() {
    if (scene_output_index < 0 || scene_output_index > 1) return;
    SceneTarget& target = scene_targets[scene_output_index];
    if (target.image.id == SG_INVALID_ID || target.texture_view.id == SG_INVALID_ID) return;

    const bool has_output_viewport = output_width > 0 && output_height > 0;
    const int width = has_output_viewport ? output_width : sapp_width();
    const int height = has_output_viewport ? output_height : sapp_height();
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, width, height, true);
        sg_apply_scissor_rect(output_x, output_y, width, height, true);
    } else {
        sg_apply_viewport(0, 0, width, height, true);
        sg_apply_scissor_rect(0, 0, width, height, true);
    }

    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, target.image, target.texture_view, 0.0f, 0.0f, (float)width, (float)height,
                         0.0f, white, false, nullptr);

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
    scene_targets[0] = SceneTarget{};
    scene_targets[1] = SceneTarget{};
    scene_output_index = -1;
    renderer_cleanup(&ctx.renderer);
}
