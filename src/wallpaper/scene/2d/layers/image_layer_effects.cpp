#include "core/logger.h"
#include "image_layer.h"

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
    for (int index = 0; index < 2; ++index) {
        effect_texture_views[index] = {};
        effect_attachment_views[index] = {};
        effect_images[index] = {};
    }
    effect_target_width = source_desc.width;
    effect_target_height = source_desc.height;
    effect_output_image = {SG_INVALID_ID};
    effect_output_view = {SG_INVALID_ID};
    for (int index = 0; index < 2; ++index) {
        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = effect_target_width;
        image_desc.height = effect_target_height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        image_desc.sample_count = 1;
        effect_images[index] = sg_make_image(&image_desc);
        if (effect_images[index].id == SG_INVALID_ID) {
            effect_log.error("Failed to create effect ping-pong image for layer %s", name.c_str());
            return false;
        }
        sg_view_desc texture_view_desc = {};
        texture_view_desc.texture.image = effect_images[index];
        effect_texture_views[index] = sg_make_view(&texture_view_desc);
        sg_view_desc attachment_view_desc = {};
        attachment_view_desc.color_attachment.image = effect_images[index];
        effect_attachment_views[index] = sg_make_view(&attachment_view_desc);
        if (effect_texture_views[index].id == SG_INVALID_ID || effect_attachment_views[index].id == SG_INVALID_ID) {
            effect_log.error("Failed to create effect ping-pong views for layer %s", name.c_str());
            return false;
        }
    }
    return true;
}
