#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "image_layer.h"
#include "image_parser.h"
#include "shared/core/engine_context.h"
#include "shared/core/load_trace.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/tree/scene_tree.h"

ImageLayer* ImageLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    ImageLayer* layer = createBaseFromDocument(doc, ctx);
    if (!layer) return nullptr;
    for (const auto& effect_doc : doc.effects) layer->addEffectFromDocument(effect_doc, ctx);
    return layer;
}

ImageLayer* ImageLayer::createBaseFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    const auto trace_start = std::chrono::steady_clock::now();
    const ImageObjectConfig config = ImageParser::parse(doc);
    ImageLayer* layer = new ImageLayer(config.name.c_str(), (sg_image){SG_INVALID_ID});
    layer->initFromDocument(doc, ctx, false);
    layer->size[0] = config.width;
    layer->size[1] = config.height;
    layer->tint[0] = config.color[0];
    layer->tint[1] = config.color[1];
    layer->tint[2] = config.color[2];
    layer->tint[3] = config.alpha;
    layer->alignment = doc.alignment;
    layer->copy_background = doc.image.copy_background;
    layer->cursor_solid = doc.image.solid;
    layer->color_blend_mode = doc.image.color_blend_mode;
    layer->alpha_document = doc.image;
    if (!doc.image.alpha_script.empty()) {
        auto script = std::make_unique<SceneScript>();
        script->setLayerId(doc.node.id);
        script->setProperty("alpha");
        if (script->load(doc.image.alpha_script, doc.image.alpha_script_properties_json) && script->valid()) {
            layer->alpha_script_value = doc.image.alpha;
            layer->alpha_script = std::move(script);
            LOG_I("Image layer '%s': alpha SceneScript loaded", doc.name.c_str());
        }
    }

    const auto trace_properties = std::chrono::steady_clock::now();
    if (!config.asset_path.empty()) {
        if (config.is_model || config.asset_path.find(".json") != std::string::npos)
            layer->loadModel(config.asset_path.c_str(), ctx);
        else
            layer->img = ctx.asset_mgr->resolveTexture(config.asset_path.c_str(), &layer->path);

        if (layer->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(layer->img);
            if (layer->size[0] == 0) {
                layer->size[0] = (float)desc.width;
                layer->size[1] = (float)desc.height;
            }
            const auto* v = ctx.asset_mgr->findVideoTexture(layer->img);
            if (!v && !layer->path.empty()) v = ctx.asset_mgr->findVideoTexture(layer->path);
            if (v && v->decoder) layer->bound_video_decoder = v->decoder.get();
        }
    }

    const auto trace_asset = std::chrono::steady_clock::now();
    if (!layer->path.empty() && !layer->bound_video_decoder) {
        layer->texture_metadata = wallpaper_engine::inspectTextureMetadata(layer->path.c_str());
        if (config.width == 0.0f && !layer->texture_metadata.animation_frames.empty()) {
            layer->size[0] = layer->texture_metadata.animation_frames.front().width;
            layer->size[1] = layer->texture_metadata.animation_frames.front().height;
        }
    }
    if (load_trace::enabled())
        LOG_TAG_I("LOAD_TRACE", "image=%s properties_ms=%.3f asset_ms=%.3f metadata_ms=%.3f", doc.name.c_str(),
                  load_trace::milliseconds(trace_properties - trace_start),
                  load_trace::milliseconds(trace_asset - trace_properties),
                  load_trace::milliseconds(std::chrono::steady_clock::now() - trace_asset));
    return layer;
}

void ImageLayer::addEffectFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx) {
    if (Effect* effect = Effect::loadFromDocument(doc, ctx)) effects.push_back(effect);
}

void ImageLayer::loadMaterial(const char* mat_rel_path, EngineContext& ctx) {
    const auto trace_start = std::chrono::steady_clock::now();
    img = ctx.asset_mgr->resolveMaterialTexture(mat_rel_path, &path, &ctx.user_properties);
    updateCachedView();
    const auto* v = ctx.asset_mgr->findVideoTexture(img);
    if (!v && !path.empty()) v = ctx.asset_mgr->findVideoTexture(path);
    if (v && v->decoder) bound_video_decoder = v->decoder.get();
    if (!path.empty() && path[0] != '$' && !bound_video_decoder) {
        const auto trace_texture = std::chrono::steady_clock::now();
        source_content = ctx.asset_mgr->textureContentBounds(path.c_str());
        const auto trace_bounds = std::chrono::steady_clock::now();
        source_opaque = ctx.asset_mgr->textureIsOpaque(path.c_str());
        if (load_trace::enabled())
            LOG_TAG_I("LOAD_TRACE", "material=%s texture_ms=%.3f content_bounds_ms=%.3f opacity_ms=%.3f", mat_rel_path,
                      load_trace::milliseconds(trace_texture - trace_start),
                      load_trace::milliseconds(trace_bounds - trace_texture),
                      load_trace::milliseconds(std::chrono::steady_clock::now() - trace_bounds));
    }
}

void ImageLayer::loadModel(const char* mdl_rel_path, EngineContext& ctx) {
    char abs_path[1024];
    if (!ctx.asset_mgr->resolvePath(mdl_rel_path, abs_path, sizeof(abs_path))) return;
    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return;
    cJSON* mdl_json = cJSON_Parse(json_str);
    free(json_str);
    if (!mdl_json) return;
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(mdl_json, "material");
    const bool is_compose = strstr(mdl_rel_path, "composelayer") != nullptr;
    is_compose_region = is_compose && size[0] > 0.0f && size[1] > 0.0f;
    is_fullscreen = !is_compose_region && (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mdl_json, "fullscreen")) ||
                                           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mdl_json, "passthrough")) ||
                                           strstr(mdl_rel_path, "fullscreenlayer") != nullptr || is_compose);
    if (is_fullscreen || is_compose_region) {
        copy_background = true;
    }
    if (is_fullscreen) {
        if (size[0] <= 0.0f || size[1] <= 0.0f) {
            size[0] = ctx.scene.scene_w > 0.0f ? ctx.scene.scene_w : 3840.0f;
            size[1] = ctx.scene.scene_h > 0.0f ? ctx.scene.scene_h : 2160.0f;
        }
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mdl_json, "solidlayer"))) {
        solid_layer = true;
        const int width = std::max(
            1, (int)std::lround(size[0] > 0.0f ? size[0] : (ctx.scene.scene_w > 0.0f ? ctx.scene.scene_w : 3840.0f)));
        const int height = std::max(
            1, (int)std::lround(size[1] > 0.0f ? size[1] : (ctx.scene.scene_h > 0.0f ? ctx.scene.scene_h : 2160.0f)));
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
    cJSON* puppet_ref = cJSON_GetObjectItemCaseSensitive(mdl_json, "puppet");
    if (!is_fullscreen && cJSON_IsString(puppet_ref)) {
        loadPuppet(puppet_ref->valuestring, ctx);
    }
    cJSON_Delete(mdl_json);
}
