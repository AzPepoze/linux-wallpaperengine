#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "image_layer.h"
#include "image_parser.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/tree/scene_tree.h"

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
