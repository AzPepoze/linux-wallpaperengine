#include "image_layer.h"

#include <string.h>

#include <algorithm>
#include <cmath>

#include "core/context.h"
#include "core/engine_context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "render/render.h"
#include "wallpaper/scene/2d/parallax.h"
#include "wallpaper/scene/graph/scene_graph.h"

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
    const sg_image source_image = img;
    const sg_view source_view = cached_view;
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

            int target_width = effect_target_width;
            int target_height = effect_target_height;
            EffectTarget* named_target = nullptr;
            if (!pass->render_target.empty()) {
                target_width = std::max(1, (int)std::lround(effect_target_width / pass->render_scale));
                target_height = std::max(1, (int)std::lround(effect_target_height / pass->render_scale));
                auto& target = named_effect_targets[pass->render_target];
                if (target.width != target_width || target.height != target_height ||
                    target.image.id == SG_INVALID_ID) {
                    target = {};
                    sg_image_desc image_desc = {};
                    image_desc.usage.color_attachment = true;
                    image_desc.width = target_width;
                    image_desc.height = target_height;
                    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
                    target.image = sg_make_image(&image_desc);
                    sg_view_desc texture_desc = {};
                    texture_desc.texture.image = target.image;
                    target.texture_view = sg_make_view(&texture_desc);
                    sg_view_desc attachment_desc = {};
                    attachment_desc.color_attachment.image = target.image;
                    target.attachment_view = sg_make_view(&attachment_desc);
                    target.width = target_width;
                    target.height = target_height;
                }
                named_target = &target;
            }

            sg_pass offscreen_pass = {};
            offscreen_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            offscreen_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            offscreen_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
            offscreen_pass.attachments.colors[0] =
                named_target ? named_target->attachment_view : effect_attachment_views[write_index];
            sg_begin_pass(&offscreen_pass);
            renderer_update_viewport(&ctx.renderer, (float)target_width, (float)target_height);

            float effect_tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            render_effect_pass_t render_pass = pass->getRenderPass();

            sg_image shader_input_image = input_image;
            sg_view shader_input_view = input_view;
            if (pass->pass_textures.texture0.id != SG_INVALID_ID &&
                pass->pass_textures.texture0_view.id != SG_INVALID_ID) {
                shader_input_image = pass->pass_textures.texture0;
                shader_input_view = pass->pass_textures.texture0_view;
            }

            std::vector<sg_view> override_views(11, sg_view{SG_INVALID_ID});
            bool has_overrides = false;
            for (const auto& [slot, binding] : pass->render_texture_bindings) {
                if (slot < 0 || slot > 11) continue;
                if (binding == "previous") {
                    if (slot == 0) {
                        shader_input_image = source_image;
                        shader_input_view = source_view;
                    } else {
                        override_views[slot - 1] = source_view;
                    }
                    has_overrides = true;
                } else {
                    auto target = named_effect_targets.find(binding);
                    if (target == named_effect_targets.end()) continue;
                    if (slot == 0) {
                        shader_input_image = target->second.image;
                        shader_input_view = target->second.texture_view;
                    } else {
                        override_views[slot - 1] = target->second.texture_view;
                    }
                    has_overrides = true;
                }
            }
            if (has_overrides) {
                render_pass.override_views = override_views.data();
                render_pass.num_override_views = override_views.size();
            }

            renderer_draw_sprite(ctx, &ctx.renderer, shader_input_image, shader_input_view, 0.0f, 0.0f,
                                 (float)target_width, (float)target_height, 0.0f, effect_tint, false, &render_pass);

            sg_end_pass();

            if (named_target) {
                input_image = named_target->image;
                input_view = named_target->texture_view;
            } else {
                input_image = effect_images[write_index];
                input_view = effect_texture_views[write_index];
                effect_output_index = write_index;
                write_index = 1 - write_index;
            }
            rendered_any = true;
        }
    }

    renderer_update_viewport(&ctx.renderer, saved_view_width, saved_view_height);
    if (!rendered_any) effect_output_index = -1;
}

ImageLayer* ImageLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    ImageLayer* layer = new ImageLayer(doc.name.empty() ? "Layer" : doc.name.c_str(), (sg_image){SG_INVALID_ID});
    layer->initFromDocument(doc, ctx);
    layer->size[0] = doc.image.size[0];
    layer->size[1] = doc.image.size[1];

    const std::string& asset_path = !doc.image.image.empty() ? doc.image.image : doc.image.model;
    if (!asset_path.empty()) {
        if (asset_path.find(".json") != std::string::npos)
            layer->loadModel(asset_path.c_str(), ctx);
        else
            layer->img = ctx.asset_mgr.resolveTexture(asset_path.c_str(), &layer->path);

        if (layer->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(layer->img);
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

    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_rotation = rotation;

    if (scene_object_id != 0 && ctx.scene_graph) {
        if (const SceneGraphNode* node = ctx.scene_graph->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
            layer_rotation = node->angles[2];
        }
        ctx.scene_graph->worldPosition(scene_object_id, layer_origin);
    }

    float rw = size[0] * layer_scale[0] * ctx.render_scale;
    float rh = size[1] * layer_scale[1] * ctx.render_scale;

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);

    float rx = ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - (rw * 0.5f);
    float ry = ctx.offset_y + (layer_origin[1] + camera_offset.y) * ctx.render_scale - (rh * 0.5f);

    sg_image draw_image = img;
    sg_view draw_view = cached_view;
    if (effect_output_index >= 0) {
        draw_image = effect_images[effect_output_index];
        draw_view = effect_texture_views[effect_output_index];
    }

    renderer_draw_sprite(ctx, &ctx.renderer, draw_image, draw_view, rx, ry, rw, rh, layer_rotation, tint, false,
                         nullptr);
}

void ImageLayer::drawDebug(EngineContext& ctx) {
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_origin[3] = {origin[0], origin[1], origin[2]};

    if (scene_object_id != 0 && ctx.scene_graph) {
        if (const SceneGraphNode* node = ctx.scene_graph->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
        }
        ctx.scene_graph->worldPosition(scene_object_id, layer_origin);
    }

    float rw = size[0] * layer_scale[0] * ctx.render_scale;
    float rh = size[1] * layer_scale[1] * ctx.render_scale;
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    float rx = ctx.offset_x + (layer_origin[0] + camera_offset.x) * ctx.render_scale - (rw * 0.5f);
    float ry = ctx.offset_y + (layer_origin[1] + camera_offset.y) * ctx.render_scale - (rh * 0.5f);

    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&ctx.renderer, rx, ry, rw, rh, color);
}
