#include "image_layer.h"
#include "shared/core/logger.h"

bool ImageLayer::ensureEffectTargets(sg_image source_image) {
    sg_image target_source = source_image.id != SG_INVALID_ID ? source_image : (sg_image)img;
    if (target_source.id == SG_INVALID_ID) return false;
    const sg_image_desc source_desc = sg_query_image_desc(target_source);
    if (source_desc.width <= 0 || source_desc.height <= 0) return false;
    if (effect_target_width == source_desc.width && effect_target_height == source_desc.height &&
        effect_targets[0].image.id != SG_INVALID_ID && effect_targets[1].image.id != SG_INVALID_ID &&
        effect_targets[0].texture_view.id != SG_INVALID_ID && effect_targets[1].texture_view.id != SG_INVALID_ID &&
        effect_targets[0].attachment_view.id != SG_INVALID_ID &&
        effect_targets[1].attachment_view.id != SG_INVALID_ID) {
        return true;
    }
    for (int index = 0; index < 2; ++index) {
        effect_targets[index].reset();
    }
    effect_target_width = source_desc.width;
    effect_target_height = source_desc.height;
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
