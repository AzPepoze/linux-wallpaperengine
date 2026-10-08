#include "effect_resolution.h"
#include "image_layer.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "wallpaper/2d/effects/effect.h"

bool ImageLayer::ensureEffectTargets(EngineContext& ctx, sg_image source_image) {
    sg_image target_source = source_image.id != SG_INVALID_ID ? source_image : (sg_image)img;
    if (target_source.id == SG_INVALID_ID) return false;
    const sg_image_desc source_desc = sg_query_image_desc(target_source);
    if (source_desc.width <= 0 || source_desc.height <= 0) return false;
    int width = source_desc.width;
    int height = source_desc.height;
    // Layers larger than the screen run effects at on-screen size; resolution uniforms keep the authored size.
    bool eligible =
        !ctx.native_effect_resolution && !is_fullscreen && !copy_background && !is_compose_region && !effects.empty();
    for (const Effect* effect : effects) {
        if (!effect || effect->passes.empty()) {
            eligible = false;
            break;
        }
        for (const ShaderPass* pass : effect->passes) {
            bool composite_binding = false;
            if (pass) {
                for (const auto& [slot, binding] : pass->render_texture_bindings)
                    composite_binding = composite_binding || effect_resolution::isCompositeBinding(binding);
            }
            if (!pass || pass->pixel_exact || composite_binding || pass->pass_textures.texture0.id != SG_INVALID_ID) {
                eligible = false;
                break;
            }
        }
        if (!eligible) break;
    }
    if (eligible) {
        const ScreenRect rect = screenRect(ctx);
        const double sx = ctx.renderer.view_width > 0 && ctx.scene.physical_view_width > 0
                              ? ctx.scene.physical_view_width / ctx.renderer.view_width
                              : 1.0;
        const double sy = ctx.renderer.view_height > 0 && ctx.scene.physical_view_height > 0
                              ? ctx.scene.physical_view_height / ctx.renderer.view_height
                              : 1.0;
        const double angle = rect.rotation * 3.141592653589793 / 180.0;
        const double c = std::cos(angle), s = std::sin(angle);
        const auto dimensions = effect_resolution::targetSize(width, height, rect.width * std::hypot(c * sx, s * sy),
                                                              rect.height * std::hypot(s * sx, c * sy));
        width = dimensions.first;
        height = dimensions.second;
    }
    effect_logical_scale = std::max(1.0f, (float)source_desc.width / (float)std::max(1, width));
    if (effect_target_width == width && effect_target_height == height && effect_targets[0].image.id != SG_INVALID_ID &&
        effect_targets[1].image.id != SG_INVALID_ID && effect_targets[0].texture_view.id != SG_INVALID_ID &&
        effect_targets[1].texture_view.id != SG_INVALID_ID && effect_targets[0].attachment_view.id != SG_INVALID_ID &&
        effect_targets[1].attachment_view.id != SG_INVALID_ID) {
        return true;
    }
    for (int index = 0; index < 2; ++index) {
        effect_targets[index].reset();
    }
    effect_target_width = width;
    effect_target_height = height;
    effect_output_image = {SG_INVALID_ID};
    effect_output_view = {SG_INVALID_ID};
    for (int index = 0; index < 2; ++index) {
        if (!effect_targets[index].create(effect_target_width, effect_target_height)) {
            effect_log.error("Failed to create effect ping-pong target for layer %s", name.c_str());
            return false;
        }
    }
    return true;
}

bool ImageLayer::prewarmEffectTargetsStep(EngineContext& ctx, size_t& cursor, sg_image source_image) {
    const sg_image target_source = source_image.id != SG_INVALID_ID ? source_image : (sg_image)img;
    if (effects.empty() || target_source.id == SG_INVALID_ID) return true;
    if (cursor == 0) {
        ensureEffectTargets(ctx, target_source);
        cursor = 1;
        return false;
    }

    size_t pass_cursor = 1;
    for (const Effect* effect : effects) {
        if (!effect) continue;
        for (const ShaderPass* pass : effect->passes) {
            if (pass_cursor++ != cursor) continue;
            if (!pass->render_target.empty() && effect_target_width > 0 && effect_target_height > 0) {
                const float scale = pass->render_scale > 0.0f ? pass->render_scale : 1.0f;
                const int width = std::max(1, (int)std::lround(effect_target_width / scale));
                const int height = std::max(1, (int)std::lround(effect_target_height / scale));
                auto& target = named_effect_targets[pass->render_target];
                target.ensureSize(width, height, pass->render_target);
            }
            ++cursor;
            return false;
        }
    }
    return true;
}
