#include "image_layer.h"

#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "shared/core/build_config.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/parser/image_parser.h"
#include "wallpaper/2d/tree/scene_tree.h"

ImageLayer::ImageLayer(const char* name, GfxImage img) : Layer(name), img(std::move(img)) {}

ImageLayer::~ImageLayer() {}

void ImageLayer::updateCachedView() {
    if (img.id != SG_INVALID_ID) {
        sg_view_desc v_desc = {};
        v_desc.texture.image = img;
        cached_view = sg_make_view(&v_desc);
    }
}

#if DEBUG_BUILD
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#endif

void ImageLayer::renderEffectChain(EngineContext& ctx, sg_image src_img, sg_view src_view) {
    effect_output_image = {SG_INVALID_ID};
    effect_output_view = {SG_INVALID_ID};
    sg_image base_img = src_img.id != SG_INVALID_ID ? src_img : (sg_image)img;
    sg_view base_view = src_view.id != SG_INVALID_ID ? src_view : (sg_view)cached_view;
    if (effects.empty() || base_img.id == SG_INVALID_ID) return;
    if (base_view.id == SG_INVALID_ID) {
        if (src_img.id != SG_INVALID_ID) {
            sg_view_desc v_desc = {};
            v_desc.texture.image = src_img;
            base_view = sg_make_view(&v_desc);
        } else {
            updateCachedView();
            base_view = cached_view;
        }
    }
    if (base_view.id == SG_INVALID_ID || !ensureEffectTargets(base_img)) return;

#if DEBUG_BUILD
    RenderDiagnostics& diag = RenderDiagnostics::instance();
#endif

    bool any_effect_solo = false;
    for (auto effect : effects) {
        if (effect && effect->solo) {
            any_effect_solo = true;
            break;
        }
    }

    sg_image input_image = base_img;
    sg_view input_view = base_view;
    int write_index = 0;
    bool rendered_any = false;
    int draw_order = 0;

#if DEBUG_BUILD
    diag.onSourceImage(0, input_image, effect_target_width, effect_target_height);
#endif

    const float saved_view_width = ctx.renderer.view_width;
    const float saved_view_height = ctx.renderer.view_height;
    renderer_update_viewport(&ctx.renderer, (float)effect_target_width, (float)effect_target_height);

    for (int eff_idx = 0; eff_idx < (int)effects.size(); ++eff_idx) {
        auto effect = effects[eff_idx];
        if (!effect) continue;
        if (!effect->visible || (any_effect_solo && !effect->solo)) continue;
#if DEBUG_BUILD
        if (!diag.isEffectIsolated(eff_idx, effect->file_path)) continue;
#endif

        const sg_image effect_source_image = input_image;
        const sg_view effect_source_view = input_view;

        for (int pass_idx = 0; pass_idx < (int)effect->passes.size(); ++pass_idx) {
            auto pass = effect->passes[pass_idx];
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

#if DEBUG_BUILD
            if (diag.isPassDisabled(pass_idx)) {
                if (named_target) {
                    // Copy-through input to named target so downstream passes don't sample uninitialized buffer
                    sg_pass copy_pass = {};
                    copy_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
                    copy_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
                    copy_pass.attachments.colors[0] = named_target->attachment_view;
                    sg_begin_pass(&copy_pass);
                    renderer_update_viewport(&ctx.renderer, (float)target_width, (float)target_height);
                    float full_white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    renderer_draw_sprite(ctx, &ctx.renderer, input_image, input_view, 0.0f, 0.0f, (float)target_width,
                                         (float)target_height, 0.0f, full_white, false, nullptr);
                    sg_end_pass();
                }
                continue;
            }
#endif

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
                // Full-frame-buffer is the accumulated scene supplied by the
                // compositor.  It must be bound once, not treated as a named
                // effect target (which would silently leave it invalid).
                if (binding == "previous" || binding == "_rt_FullFrameBuffer") {
                    if (slot == 0) {
                        shader_input_image = effect_source_image;
                        shader_input_view = effect_source_view;
                    } else {
                        override_views[slot - 1] = effect_source_view;
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
            render_pass.is_fullscreen_quad = pass->is_fullscreen_quad;

            renderer_draw_sprite(ctx, &ctx.renderer, shader_input_image, shader_input_view, 0.0f, 0.0f,
                                 (float)target_width, (float)target_height, 0.0f, effect_tint, false, &render_pass);

            sg_end_pass();

            sg_image out_img = named_target ? named_target->image : effect_images[write_index];

#if DEBUG_BUILD
            if (diag.config.enabled) {
                PassTraceEntry trace;
                trace.frame_number = ctx.profiler.frame_index;
                trace.layer_name = this->name.empty() ? ("Layer_" + std::to_string(scene_object_id)) : this->name;
                trace.effect_index = eff_idx;
                trace.effect_file = effect->file_path;
                trace.pass_index = pass_idx;
                trace.shader_name = pass->shader_name;
                trace.enabled = pass->enabled;
                trace.visible = effect->visible;
                trace.draw_order = draw_order++;
                trace.render_target_name = pass->render_target;
                trace.target_image_id = out_img.id;
                trace.target_view_id =
                    named_target ? named_target->attachment_view.id : effect_attachment_views[write_index].id;
                trace.target_width = target_width;
                trace.target_height = target_height;
                trace.target_pixel_format = "RGBA8";
                trace.render_scale = pass->render_scale;
                trace.is_fullscreen_quad = pass->is_fullscreen_quad;

                TextureBindingTrace in0;
                in0.slot = 0;
                in0.image_id = shader_input_image.id;
                in0.view_id = shader_input_view.id;
                sg_image_desc in0_d = sg_query_image_desc(shader_input_image);
                in0.width = in0_d.width;
                in0.height = in0_d.height;
                in0.pixel_format = "RGBA8";
                in0.is_render_target = in0_d.usage.color_attachment;
                in0.semantic_source =
                    pass->render_texture_bindings.count(0) ? pass->render_texture_bindings.at(0) : "previous";
                trace.inputs.push_back(in0);

                for (const auto& [slot, binding] : pass->render_texture_bindings) {
                    if (slot == 0) continue;
                    TextureBindingTrace in_b;
                    in_b.slot = slot;
                    in_b.semantic_source = binding;
                    if (binding == "previous" || binding == "_rt_FullFrameBuffer") {
                        in_b.image_id = effect_source_image.id;
                        in_b.view_id = effect_source_view.id;
                        sg_image_desc d = sg_query_image_desc(effect_source_image);
                        in_b.width = d.width;
                        in_b.height = d.height;
                        in_b.is_render_target = d.usage.color_attachment;
                    } else {
                        auto target_it = named_effect_targets.find(binding);
                        if (target_it != named_effect_targets.end()) {
                            in_b.image_id = target_it->second.image.id;
                            in_b.view_id = target_it->second.texture_view.id;
                            in_b.width = target_it->second.width;
                            in_b.height = target_it->second.height;
                            in_b.is_render_target = true;
                        }
                    }
                    trace.inputs.push_back(in_b);
                }

                gpu_set_image_debug_label(out_img, (pass->shader_name + " Target").c_str());
                diag.recordPass(trace, out_img);
            }
#endif

            if (named_target) {
                input_image = named_target->image;
                input_view = named_target->texture_view;
            } else {
                input_image = effect_images[write_index];
                input_view = effect_texture_views[write_index];
                write_index = 1 - write_index;
            }
            effect_output_image = input_image;
            effect_output_view = input_view;
            rendered_any = true;

#if DEBUG_BUILD
            if (diag.shouldStopAfterPass(pass_idx)) {
                break;
            }
#endif
        }
    }

    renderer_update_viewport(&ctx.renderer, saved_view_width, saved_view_height);
    if (!rendered_any) {
        effect_output_image = {SG_INVALID_ID};
        effect_output_view = {SG_INVALID_ID};
    } else {
#if DEBUG_BUILD
        diag.onLayerFinalImage(0, effect_output_image, effect_target_width, effect_target_height);
#endif
    }
}

ImageLayer* ImageLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    const ImageObjectConfig config = ImageParser::parse(doc);
    ImageLayer* layer = new ImageLayer(config.name.c_str(), (sg_image){SG_INVALID_ID});
    layer->initFromDocument(doc, ctx);
    layer->size[0] = config.width;
    layer->size[1] = config.height;
    layer->tint[0] = config.color[0];
    layer->tint[1] = config.color[1];
    layer->tint[2] = config.color[2];
    layer->tint[3] = config.alpha;
    layer->copy_background = doc.image.copy_background;
    layer->color_blend_mode = doc.image.color_blend_mode;
    layer->alpha_document = doc.image;

    if (!config.asset_path.empty()) {
        if (config.is_model || config.asset_path.find(".json") != std::string::npos)
            layer->loadModel(config.asset_path.c_str(), ctx);
        else
            layer->img = ctx.asset_mgr.resolveTexture(config.asset_path.c_str(), &layer->path);

        if (layer->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(layer->img);
            if (layer->size[0] == 0) {
                layer->size[0] = (float)desc.width;
                layer->size[1] = (float)desc.height;
            }
            const auto* v = ctx.asset_mgr.findVideoTexture(layer->img);
            if (!v && !layer->path.empty()) v = ctx.asset_mgr.findVideoTexture(layer->path);
            if (v && v->decoder) layer->bound_video_decoder = v->decoder.get();
        }
    }

    return layer;
}

void ImageLayer::loadMaterial(const char* mat_rel_path, EngineContext& ctx) {
    img = ctx.asset_mgr.resolveMaterialTexture(mat_rel_path, &path);
    updateCachedView();
    const auto* v = ctx.asset_mgr.findVideoTexture(img);
    if (!v && !path.empty()) v = ctx.asset_mgr.findVideoTexture(path);
    if (v && v->decoder) bound_video_decoder = v->decoder.get();
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
    is_fullscreen = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mdl_json, "fullscreen")) ||
                    cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mdl_json, "passthrough")) ||
                    strstr(mdl_rel_path, "fullscreenlayer") != nullptr ||
                    strstr(mdl_rel_path, "composelayer") != nullptr;
    if (is_fullscreen) {
        copy_background = true;
        if (size[0] <= 0.0f || size[1] <= 0.0f) {
            size[0] = ctx.scene_w > 0.0f ? ctx.scene_w : 3840.0f;
            size[1] = ctx.scene_h > 0.0f ? ctx.scene_h : 2160.0f;
        }
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mdl_json, "solidlayer")) || is_fullscreen) {
        solid_layer = true;
        const int width =
            std::max(1, (int)std::lround(size[0] > 0.0f ? size[0] : (ctx.scene_w > 0.0f ? ctx.scene_w : 3840.0f)));
        const int height =
            std::max(1, (int)std::lround(size[1] > 0.0f ? size[1] : (ctx.scene_h > 0.0f ? ctx.scene_h : 2160.0f)));
        std::vector<uint32_t> pixels((size_t)width * (size_t)height, 0xFFFFFFFFu);
        sg_image_desc image_desc = {};
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        image_desc.data.mip_levels[0] = {pixels.data(), pixels.size() * sizeof(uint32_t)};
        img = sg_make_image(&image_desc);
        updateCachedView();
    } else if (cJSON_IsString(mat_ref)) {
        loadMaterial(mat_ref->valuestring, ctx);
    }
    cJSON_Delete(mdl_json);
}

void ImageLayer::update(float dt, EngineContext& ctx) {
    (void)dt;
    tint[3] = evaluateImageAlpha(alpha_document, ctx.time);
    if (!is_fullscreen) {
        renderEffectChain(ctx);
    }
}

void ImageLayer::start() {
    if (bound_video_decoder) bound_video_decoder->start();
}

void ImageLayer::stop() {
    if (bound_video_decoder) bound_video_decoder->stop();
}

void ImageLayer::pause() {
    if (bound_video_decoder) bound_video_decoder->pause();
}

void ImageLayer::resume() {
    if (bound_video_decoder) bound_video_decoder->resume();
}

bool ImageLayer::requiresSceneColor() const {
    return copy_background || color_blend_mode != 0;
}
