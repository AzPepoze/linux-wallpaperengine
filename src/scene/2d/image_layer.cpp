#include "image_layer.h"

#include <string.h>

#include "../../core/context.h"
#include "../../core/engine_context.h"
#include "../../core/logger.h"
#include "../../core/utils.h"
#include "../../render/render.h"
#include "../parallax.h"

ImageLayer::ImageLayer(const char* name, GfxImage img) : Layer(name), img(std::move(img)) {}

ImageLayer::~ImageLayer() {}

void ImageLayer::updateCachedView() {
    if (img.id != SG_INVALID_ID) {
        sg_view_desc v_desc = {};
        v_desc.texture.image = img;
        cached_view = sg_make_view(&v_desc);
    }
}

bool ImageLayer::ensureEffectTargets() {
    if (img.id == SG_INVALID_ID) return false;

    const sg_image_desc source_desc = sg_query_image_desc(img);
    if (source_desc.width <= 0 || source_desc.height <= 0) return false;

    if (effect_target_width == source_desc.width && effect_target_height == source_desc.height &&
        effect_images[0].id != SG_INVALID_ID && effect_images[1].id != SG_INVALID_ID &&
        effect_texture_views[0].id != SG_INVALID_ID && effect_texture_views[1].id != SG_INVALID_ID &&
        effect_attachment_views[0].id != SG_INVALID_ID && effect_attachment_views[1].id != SG_INVALID_ID) {
        return true;
    }

    // Views must be released before their backing images.
    for (int i = 0; i < 2; ++i) {
        effect_texture_views[i] = {};
        effect_attachment_views[i] = {};
        effect_images[i] = {};
    }

    effect_target_width = source_desc.width;
    effect_target_height = source_desc.height;
    effect_output_index = -1;

    for (int i = 0; i < 2; ++i) {
        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = effect_target_width;
        image_desc.height = effect_target_height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        image_desc.sample_count = 1;
        effect_images[i] = sg_make_image(&image_desc);
        if (effect_images[i].id == SG_INVALID_ID) {
            effect_log.error("Failed to create effect ping-pong image for layer %s", name.c_str());
            return false;
        }

        sg_view_desc texture_view_desc = {};
        texture_view_desc.texture.image = effect_images[i];
        effect_texture_views[i] = sg_make_view(&texture_view_desc);

        sg_view_desc attachment_view_desc = {};
        attachment_view_desc.color_attachment.image = effect_images[i];
        effect_attachment_views[i] = sg_make_view(&attachment_view_desc);

        if (effect_texture_views[i].id == SG_INVALID_ID || effect_attachment_views[i].id == SG_INVALID_ID) {
            effect_log.error("Failed to create effect ping-pong views for layer %s", name.c_str());
            return false;
        }
    }

    return true;
}

void ImageLayer::renderEffectChain(EngineContext& ctx) {
    effect_output_index = -1;
    if (effects.empty() || img.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();
    if (cached_view.id == SG_INVALID_ID || !ensureEffectTargets()) return;

    bool any_effect_solo = false;
    for (auto effect : effects) {
        if (effect && effect->solo) {
            any_effect_solo = true;
            break;
        }
    }

    sg_image input_image = img;
    sg_view input_view = cached_view;
    int write_index = 0;
    bool rendered_any = false;

    const float saved_view_width = ctx.renderer.view_width;
    const float saved_view_height = ctx.renderer.view_height;
    renderer_update_viewport(&ctx.renderer, (float)effect_target_width, (float)effect_target_height);

    for (auto effect : effects) {
        if (!effect) continue;
        if (any_effect_solo ? !effect->solo : !effect->visible) continue;

        for (auto pass : effect->passes) {
            if (!pass || !pass->enabled || pass->compiled.pipeline.id == SG_INVALID_ID) continue;

            if (pass->shader_name.find("depthparallax") != std::string::npos && !path.empty() &&
                strstr(path.c_str(), ".tex")) {
                pass->resolveDepth(path.c_str(), ctx);
            }

            sg_pass offscreen_pass = {};
            offscreen_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            offscreen_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            offscreen_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
            offscreen_pass.attachments.colors[0] = effect_attachment_views[write_index];
            sg_begin_pass(&offscreen_pass);

            float effect_tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, input_image, input_view, 0.0f, 0.0f,
                                 (float)effect_target_width, (float)effect_target_height, 0.0f, effect_tint, false, pass);

            sg_end_pass();

            input_image = effect_images[write_index];
            input_view = effect_texture_views[write_index];
            effect_output_index = write_index;
            write_index = 1 - write_index;
            rendered_any = true;
        }
    }

    renderer_update_viewport(&ctx.renderer, saved_view_width, saved_view_height);
    if (!rendered_any) effect_output_index = -1;
}

ImageLayer* ImageLayer::createFromJSON(cJSON* node, EngineContext& ctx) {
    ImageLayer* layer = new ImageLayer("Layer", (sg_image){SG_INVALID_ID});
    layer->loadBaseProperties(node, ctx);

    cJSON* size_node = cJSON_GetObjectItemCaseSensitive(node, "size");
    if (cJSON_IsString(size_node)) {
        sscanf(size_node->valuestring, "%f %f", &layer->size[0], &layer->size[1]);
    }

    cJSON* asset_path = cJSON_GetObjectItemCaseSensitive(node, "image");
    if (!cJSON_IsString(asset_path)) asset_path = cJSON_GetObjectItemCaseSensitive(node, "model");

    if (cJSON_IsString(asset_path)) {
        if (strstr(asset_path->valuestring, ".json"))
            layer->loadModel(asset_path->valuestring, ctx);
        else
            layer->img = ctx.asset_mgr.resolveTexture(asset_path->valuestring, &layer->path);

        if (layer->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(layer->img);
            // If size wasn't in JSON, use asset size
            if (layer->size[0] == 0) {
                layer->size[0] = (float)desc.width;
                layer->size[1] = (float)desc.height;
            }
        }
    }

    return layer;
}

void ImageLayer::loadMaterial(const char* mat_rel_path, EngineContext& ctx) {
    img = ctx.asset_mgr.resolveMaterialTexture(mat_rel_path, &path);
    updateCachedView();
}

void ImageLayer::loadModel(const char* mdl_rel_path, EngineContext& ctx) {
    char abs_path[1024];
    if (!ctx.asset_mgr.resolvePath(mdl_rel_path, abs_path, sizeof(abs_path))) return;
    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return;
    cJSON* mdl_json = cJSON_Parse(json_str);
    free(json_str);
    if (!mdl_json) return;
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(mdl_json, "material");
    if (cJSON_IsString(mat_ref)) loadMaterial(mat_ref->valuestring, ctx);
    cJSON_Delete(mdl_json);
}

void ImageLayer::update(float dt, EngineContext& ctx) {
    (void)dt;
    renderEffectChain(ctx);
}

void ImageLayer::draw(EngineContext& ctx) {
    if (img.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();

    float rw = size[0] * scale[0] * ctx.render_scale;
    float rh = size[1] * scale[1] * ctx.render_scale;

    const parallax_offset_t camera_offset =
        parallax_layer_offset(ctx, scene_object_id, origin, parallax);

    // Center image on its parallax-adjusted scene origin.
    float rx = ctx.offset_x + (origin[0] + camera_offset.x) * ctx.render_scale - (rw * 0.5f);
    float ry = ctx.offset_y + (origin[1] + camera_offset.y) * ctx.render_scale - (rh * 0.5f);

    sg_image draw_image = img;
    sg_view draw_view = cached_view;
    if (effect_output_index >= 0) {
        draw_image = effect_images[effect_output_index];
        draw_view = effect_texture_views[effect_output_index];
    }

    // Effects have already been evaluated into the offscreen chain during update().
    renderer_draw_sprite(ctx, &ctx.renderer, draw_image, draw_view, rx, ry, rw, rh, rotation, tint, false, nullptr);
}

void ImageLayer::drawDebug(EngineContext& ctx) {
    float rw = size[0] * scale[0] * ctx.render_scale;
    float rh = size[1] * scale[1] * ctx.render_scale;
    const parallax_offset_t camera_offset =
        parallax_layer_offset(ctx, scene_object_id, origin, parallax);
    float rx = ctx.offset_x + (origin[0] + camera_offset.x) * ctx.render_scale - (rw * 0.5f);
    float ry = ctx.offset_y + (origin[1] + camera_offset.y) * ctx.render_scale - (rh * 0.5f);

    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&ctx.renderer, rx, ry, rw, rh, color);
}
