#include "image_layer.h"

#include <string.h>

#include "../../core/context.h"
#include "../../core/config.h"
#include "../../core/engine_context.h"
#include "../../core/utils.h"
#include "../../render/render.h"

ImageLayer::ImageLayer(const char* name, sg_image img) : Layer(name), img(img) {}

ImageLayer::~ImageLayer() {}

void ImageLayer::updateCachedView() {
    if (img.id != SG_INVALID_ID) {
        sg_view_desc v_desc = {};
        v_desc.texture.image = img;
        cached_view = sg_make_view(&v_desc);
    }
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
    (void)ctx;
}

void ImageLayer::draw(EngineContext& ctx) {
    if (img.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();

    float rw = size[0] * scale[0] * ctx.render_scale;
    float rh = size[1] * scale[1] * ctx.render_scale;

    float px = parallax[0] * ctx.parallax_smooth_x * Config::kParallaxScale;
    float py = parallax[1] * ctx.parallax_smooth_y * Config::kParallaxScale;

    // Center image on its origin
    float rx = ctx.offset_x + (origin[0] + px) * ctx.render_scale - (rw * 0.5f);
    float ry = ctx.offset_y + (origin[1] + py) * ctx.render_scale - (rh * 0.5f);

    ShaderPass* pass = nullptr;
    if (!effects.empty()) {
        bool any_eff_solo = false;
        for (auto eff : effects) {
            if (eff->solo) {
                any_eff_solo = true;
                break;
            }
        }

        Effect* target_eff = nullptr;
        if (any_eff_solo) {
            for (auto eff : effects) {
                if (eff->solo) {
                    target_eff = eff;
                    break;
                }
            }
        } else {
            for (auto eff : effects) {
                if (eff->visible) {
                    target_eff = eff;
                    break;
                }
            }
        }

        if (target_eff && !target_eff->passes.empty()) {
            pass = target_eff->passes[0];
            if (pass && !path.empty() && strstr(path.c_str(), ".tex")) {
                pass->resolveDepth(path.c_str(), ctx);
            }
        }
    }

    renderer_draw_sprite(ctx, &ctx.renderer, img, cached_view, rx, ry, rw, rh, rotation, tint, false, pass);
}

void ImageLayer::drawDebug(EngineContext& ctx) {
    float rw = size[0] * scale[0] * ctx.render_scale;
    float rh = size[1] * scale[1] * ctx.render_scale;
    float px = parallax[0] * ctx.parallax_smooth_x * Config::kParallaxScale;
    float py = parallax[1] * ctx.parallax_smooth_y * Config::kParallaxScale;
    float rx = ctx.offset_x + (origin[0] + px) * ctx.render_scale - (rw * 0.5f);
    float ry = ctx.offset_y + (origin[1] + py) * ctx.render_scale - (rh * 0.5f);

    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&ctx.renderer, rx, ry, rw, rh, color);
}
