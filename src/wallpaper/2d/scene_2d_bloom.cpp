#include <cjson/cJSON.h>

#include "scene_2d.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/pass_util.h"
#include "shared/graphics/passes/pass_loader.h"
#include "shared/graphics/passes/shader_pass.h"
#include "shared/graphics/render.h"
#include "sokol_app.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {
ShaderPass* createBloomPass(const char* material_relpath,
                            const std::map<std::string, std::vector<float>>& custom_constants, EngineContext& ctx) {
    char material_path[1024];
    if (!ctx.asset_mgr.resolvePath(material_relpath, material_path, sizeof(material_path))) {
        LOG_TAG_E("BLOOM", "Failed to resolve bloom material: %s", material_relpath);
        return nullptr;
    }
    cJSON* config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "material", material_relpath);
    if (!custom_constants.empty()) {
        cJSON* consts = cJSON_CreateObject();
        for (const auto& [name, val] : custom_constants) {
            if (val.size() == 1) {
                cJSON_AddNumberToObject(consts, name.c_str(), val[0]);
            } else if (val.size() > 1) {
                cJSON* arr = cJSON_CreateFloatArray(val.data(), static_cast<int>(val.size()));
                cJSON_AddItemToObject(consts, name.c_str(), arr);
            }
        }
        cJSON_AddItemToObject(config, "constants", consts);
    }

    auto* pass = new ShaderPass(config, nullptr, ctx);
    cJSON_Delete(config);
    pass->init(ctx);
    return pass;
}

}  // namespace

void Scene2DRuntime::destroyBloomPipelines() {
    delete bloom_pass_extract;
    bloom_pass_extract = nullptr;
    delete bloom_pass_blur_v;
    bloom_pass_blur_v = nullptr;
    delete bloom_pass_blur_h;
    bloom_pass_blur_h = nullptr;
    delete bloom_pass_combine;
    bloom_pass_combine = nullptr;
}

void Scene2DRuntime::initBloomPipelines() {
    destroyBloomPipelines();
    const bool hdr = ctx.scene.general.hdr;
    if (hdr) {
        const float threshold = ctx.scene.general.bloom.hdr_threshold;
        const float knee = threshold * ctx.scene.general.bloom.hdr_feather;
        const float scatter = ctx.scene.general.bloom.hdr_scatter > 0.0f ? ctx.scene.general.bloom.hdr_scatter : 1.0f;

        bloom_pass_extract = createBloomPass(
            "materials/util/hdr_downsample_bloom.json",
            {
                {"bloomstrength", {ctx.scene.general.bloom.hdr_strength}},
                {"blend", {threshold, threshold - knee, 2.0f * knee, knee > 0.0f ? 0.25f / knee : 0.0f}},
                {"bloomtint", {1.0f, 1.0f, 1.0f}},
            },
            ctx);
        bloom_pass_blur_v = createBloomPass("materials/util/hdr_downsample.json", {}, ctx);
        bloom_pass_blur_h = createBloomPass("materials/util/hdr_upsample.json", {{"scatter", {scatter}}}, ctx);
        bloom_pass_combine = createBloomPass("materials/util/combine_hdr_upsample_linear.json", {}, ctx);
    } else {
        bloom_pass_extract = createBloomPass("materials/util/downsample_quarter_bloom.json",
                                             {
                                                 {"bloomstrength", {ctx.scene.general.bloom.strength}},
                                                 {"bloomthreshold", {ctx.scene.general.bloom.threshold}},
                                                 {"bloomtint", {1.0f, 1.0f, 1.0f}},
                                             },
                                             ctx);
        bloom_pass_blur_v = createBloomPass("materials/util/downsample_eighth_blur_v.json", {}, ctx);
        bloom_pass_blur_h = createBloomPass("materials/util/blur_h_bloom.json", {}, ctx);
        bloom_pass_combine = createBloomPass("materials/util/combine_ldr.json", {}, ctx);
    }
}

bool Scene2DRuntime::ensureBloomTargets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const sg_pixel_format requested_format = compositionPixelFormat();
    if (bloom_targets[0].image.id != SG_INVALID_ID && bloom_targets[0].width == width &&
        bloom_targets[0].height == height && bloom_targets[1].image.id != SG_INVALID_ID &&
        bloom_targets[1].width == width && bloom_targets[1].height == height &&
        bloom_targets[0].pixel_format == requested_format && bloom_targets[1].pixel_format == requested_format) {
        return true;
    }

    bloom_targets[0].reset();
    bloom_targets[1].reset();

    if (!bloom_targets[0].create(width, height, requested_format) ||
        !bloom_targets[1].create(width, height, requested_format)) {
        bloom_targets[0].reset();
        bloom_targets[1].reset();
        return false;
    }
    return true;
}

int Scene2DRuntime::renderBloom(int current_target_index, int width, int height) {
    if (RenderDiagnostics::instance().getConfig().disable_bloom) return current_target_index;
    const bool hdr = ctx.scene.general.hdr;
    const float strength = hdr ? ctx.scene.general.bloom.hdr_strength : ctx.scene.general.bloom.strength;
    if (!ctx.scene.general.bloom.enabled || strength <= 0.0f) return current_target_index;
    if (!bloom_pass_extract || !bloom_pass_blur_v || !bloom_pass_blur_h || !bloom_pass_combine) {
        initBloomPipelines();
    }
    if (!bloom_pass_extract || !bloom_pass_blur_v || !bloom_pass_blur_h || !bloom_pass_combine) {
        return current_target_index;
    }

    const int bloom_w = std::max(1, width / 4);
    const int bloom_h = std::max(1, height / 4);
    if (!ensureBloomTargets(bloom_w, bloom_h)) return current_target_index;
    const auto runStage = [&](ShaderPass& pass, sg_view target, sg_image source, sg_view source_view, int target_w,
                              int target_h, sg_view bloom_view = {SG_INVALID_ID}) {
        sg_pass target_pass = colorPass(target, SG_LOADACTION_CLEAR, 1.0f);
        sg_begin_pass(&target_pass);
        renderer_update_viewport(&ctx.renderer, (float)target_w, (float)target_h);

        render_effect_pass_t pass_desc = pass.getRenderPass(ctx.profiler.frame_index, ctx.time);
        sg_view extra_views[] = {bloom_view};
        if (bloom_view.id != SG_INVALID_ID) {
            pass_desc.override_views = extra_views;
            pass_desc.num_override_views = 1;
        }
        float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer_draw_sprite(ctx, &ctx.renderer, source, source_view, 0.0f, 0.0f, (float)target_w, (float)target_h,
                             0.0f, white, false, &pass_desc);
        sg_end_pass();
    };

    const int next = 1 - current_target_index;
    const SceneTarget& scene = scene_targets[current_target_index];
    runStage(*bloom_pass_extract, bloom_targets[0].attachment_view, scene.image, scene.texture_view, bloom_w, bloom_h);
    runStage(*bloom_pass_blur_v, bloom_targets[1].attachment_view, bloom_targets[0].image,
             bloom_targets[0].texture_view, bloom_w, bloom_h);
    runStage(*bloom_pass_blur_h, bloom_targets[0].attachment_view, bloom_targets[1].image,
             bloom_targets[1].texture_view, bloom_w, bloom_h);
    runStage(*bloom_pass_combine, scene_targets[next].attachment_view, scene.image, scene.texture_view, width, height,
             bloom_targets[0].texture_view);
    return next;
}
